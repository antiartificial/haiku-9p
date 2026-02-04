/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Protocol Client Implementation
 */

#include "9p_client.h"

#include <stdlib.h>
#include <string.h>
#include <KernelExport.h>


// #pragma mark - FidPool


FidPool::FidPool()
	:
	fBitmap(NULL),
	fMaxFids(0),
	fBitmapSize(0),
	fNextHint(0)
{
	mutex_init(&fLock, "9p fid pool");
}


FidPool::~FidPool()
{
	mutex_destroy(&fLock);
	free(fBitmap);
}


status_t
FidPool::Init(uint32 maxFids)
{
	fMaxFids = maxFids;
	fBitmapSize = (maxFids + 31) / 32;
	fBitmap = (uint32*)calloc(fBitmapSize, sizeof(uint32));
	if (fBitmap == NULL)
		return B_NO_MEMORY;

	// Reserve FID 0 (used for root after attach)
	fBitmap[0] |= 1;
	fNextHint = 1;

	return B_OK;
}


uint32
FidPool::Allocate()
{
	MutexLocker locker(&fLock);

	// Start from hint
	for (uint32 i = 0; i < fMaxFids; i++) {
		uint32 fid = (fNextHint + i) % fMaxFids;
		uint32 word = fid / 32;
		uint32 bit = fid % 32;

		if ((fBitmap[word] & (1U << bit)) == 0) {
			fBitmap[word] |= (1U << bit);
			fNextHint = (fid + 1) % fMaxFids;
			return fid;
		}
	}

	return P9_NOFID;
}


void
FidPool::Release(uint32 fid)
{
	if (fid >= fMaxFids)
		return;

	MutexLocker locker(&fLock);

	uint32 word = fid / 32;
	uint32 bit = fid % 32;
	fBitmap[word] &= ~(1U << bit);
}


bool
FidPool::IsValid(uint32 fid) const
{
	if (fid >= fMaxFids)
		return false;

	uint32 word = fid / 32;
	uint32 bit = fid % 32;
	return (fBitmap[word] & (1U << bit)) != 0;
}


// #pragma mark - TagPool


TagPool::TagPool()
	:
	fBitmap(NULL),
	fMaxTags(0),
	fBitmapSize(0),
	fNextHint(0)
{
	mutex_init(&fLock, "9p tag pool");
}


TagPool::~TagPool()
{
	mutex_destroy(&fLock);
	free(fBitmap);
}


status_t
TagPool::Init(uint16 maxTags)
{
	fMaxTags = maxTags;
	fBitmapSize = (maxTags + 31) / 32;
	fBitmap = (uint32*)calloc(fBitmapSize, sizeof(uint32));
	if (fBitmap == NULL)
		return B_NO_MEMORY;

	fNextHint = 0;
	return B_OK;
}


uint16
TagPool::Allocate()
{
	MutexLocker locker(&fLock);

	for (uint16 i = 0; i < fMaxTags; i++) {
		uint16 tag = (fNextHint + i) % fMaxTags;
		uint32 word = tag / 32;
		uint32 bit = tag % 32;

		if ((fBitmap[word] & (1U << bit)) == 0) {
			fBitmap[word] |= (1U << bit);
			fNextHint = (tag + 1) % fMaxTags;
			return tag;
		}
	}

	return P9_NOTAG;
}


void
TagPool::Release(uint16 tag)
{
	if (tag >= fMaxTags || tag == P9_NOTAG)
		return;

	MutexLocker locker(&fLock);

	uint32 word = tag / 32;
	uint32 bit = tag % 32;
	fBitmap[word] &= ~(1U << bit);
}


// #pragma mark - P9Request


P9Request::P9Request(uint16 tag)
	:
	fTag(tag),
	fResponse(NULL)
{
	fCompletionSem = create_sem(0, "9p request");
}


P9Request::~P9Request()
{
	delete_sem(fCompletionSem);
	delete fResponse;
}


void
P9Request::SetResponse(P9Message* response)
{
	fResponse = response;
	release_sem(fCompletionSem);
}


status_t
P9Request::WaitForResponse(bigtime_t timeout)
{
	return acquire_sem_etc(fCompletionSem, 1, B_RELATIVE_TIMEOUT, timeout);
}


// #pragma mark - P9Client


P9Client::P9Client()
	:
	fTransport(NULL),
	fMsize(P9_DEFAULT_MSIZE),
	fIOUnit(0),
	fRootFid(0),
	fConnected(false)
{
	mutex_init(&fRequestLock, "9p client request");
	mutex_init(&fPendingLock, "9p client pending");
}


P9Client::~P9Client()
{
	Uninit();
	mutex_destroy(&fRequestLock);
	mutex_destroy(&fPendingLock);
}


status_t
P9Client::Init(P9Transport* transport, uint32 msize)
{
	if (transport == NULL)
		return B_BAD_VALUE;

	fTransport = transport;
	fMsize = msize;

	status_t status = fFidPool.Init();
	if (status != B_OK)
		return status;

	status = fTagPool.Init();
	if (status != B_OK)
		return status;

	return B_OK;
}


void
P9Client::Uninit()
{
	Disconnect();
	fTransport = NULL;
}


status_t
P9Client::Connect(const char* aname)
{
	if (fTransport == NULL)
		return B_NO_INIT;

	if (fConnected)
		return B_OK;

	// Version negotiation
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	status = request.BuildVersion(P9_NOTAG, fMsize, P9_VERSION_9P2000_L);
	if (status != B_OK)
		return status;

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	if (status != B_OK)
		return status;

	uint8 type;
	uint16 tag;
	uint32 size;
	status = response->ReadHeader(type, tag, size);
	if (status != B_OK || type != P9_RVERSION) {
		delete response;
		return status != B_OK ? status : B_ERROR;
	}

	uint32 serverMsize;
	char version[32];
	status = response->ParseVersion(serverMsize, version, sizeof(version));
	delete response;
	if (status != B_OK)
		return status;

	// Use smaller of client/server msize
	if (serverMsize < fMsize)
		fMsize = serverMsize;

	// Verify version
	if (strcmp(version, P9_VERSION_9P2000_L) != 0) {
		dprintf("9p: server doesn't support 9P2000.L (got %s)\n", version);
		return B_NOT_SUPPORTED;
	}

	// Attach to filesystem
	fRootFid = fFidPool.Allocate();
	if (fRootFid == P9_NOFID)
		return B_NO_MEMORY;

	request.Reset();
	status = request.Init();
	if (status != B_OK) {
		fFidPool.Release(fRootFid);
		return status;
	}

	uint16 attachTag = fTagPool.Allocate();
	status = request.BuildAttach(attachTag, fRootFid, P9_NOFID,
		"", aname, P9_NONUNAME);
	if (status != B_OK) {
		fTagPool.Release(attachTag);
		fFidPool.Release(fRootFid);
		return status;
	}

	status = DoRequest(&request, &response);
	fTagPool.Release(attachTag);
	if (status != B_OK) {
		fFidPool.Release(fRootFid);
		return status;
	}

	status = response->ReadHeader(type, tag, size);
	if (status != B_OK) {
		delete response;
		fFidPool.Release(fRootFid);
		return status;
	}

	if (type == P9_RLERROR) {
		uint32 error;
		response->ParseLerror(error);
		delete response;
		fFidPool.Release(fRootFid);
		return p9_error_to_haiku(error);
	}

	if (type != P9_RATTACH) {
		delete response;
		fFidPool.Release(fRootFid);
		return B_ERROR;
	}

	P9Qid qid;
	status = response->ParseAttach(qid);
	delete response;
	if (status != B_OK) {
		fFidPool.Release(fRootFid);
		return status;
	}

	// Set default IO unit
	fIOUnit = fMsize - P9_HEADER_SIZE - sizeof(uint32);

	fConnected = true;
	return B_OK;
}


void
P9Client::Disconnect()
{
	if (!fConnected)
		return;

	// Clunk root fid
	Clunk(fRootFid);
	fFidPool.Release(fRootFid);

	fConnected = false;
}


status_t
P9Client::DoRequest(P9Message* request, P9Message** response)
{
	MutexLocker locker(&fRequestLock);

	// Send request
	status_t status = fTransport->SendMessage(request->Data(), request->Size());
	if (status != B_OK)
		return status;

	// Allocate response
	*response = new(std::nothrow) P9Message(fMsize);
	if (*response == NULL)
		return B_NO_MEMORY;

	status = (*response)->Init();
	if (status != B_OK) {
		delete *response;
		*response = NULL;
		return status;
	}

	// Receive response
	size_t received = fMsize;
	status = fTransport->ReceiveMessage((*response)->Data(), &received);
	if (status != B_OK) {
		delete *response;
		*response = NULL;
		return status;
	}

	(*response)->Buffer()->SetSize(received);
	return B_OK;
}


status_t
P9Client::CheckError(P9Message* response)
{
	uint8 type;
	uint16 tag;
	uint32 size;

	status_t status = response->ReadHeader(type, tag, size);
	if (status != B_OK)
		return status;

	if (type == P9_RLERROR) {
		uint32 error;
		response->ParseLerror(error);
		return p9_error_to_haiku(error);
	}

	return B_OK;
}


status_t
P9Client::Walk(uint32 fid, uint32 newfid, const char* path, P9Qid* qid)
{
	if (path == NULL || path[0] == '\0') {
		// Clone fid
		const char* empty = NULL;
		uint16 nwqid;
		P9Qid qids[1];
		return WalkPath(fid, newfid, 0, &empty, &nwqid, qids);
	}

	// Split path into components
	char* pathCopy = strdup(path);
	if (pathCopy == NULL)
		return B_NO_MEMORY;

	// Count components
	uint16 nwname = 0;
	char* p = pathCopy;
	while (*p) {
		if (*p == '/') {
			p++;
			continue;
		}
		nwname++;
		while (*p && *p != '/')
			p++;
	}

	if (nwname == 0) {
		free(pathCopy);
		const char* empty = NULL;
		uint16 nwqid;
		P9Qid qids[1];
		return WalkPath(fid, newfid, 0, &empty, &nwqid, qids);
	}

	// Collect component pointers
	const char** wnames = (const char**)malloc(nwname * sizeof(char*));
	if (wnames == NULL) {
		free(pathCopy);
		return B_NO_MEMORY;
	}

	uint16 i = 0;
	p = pathCopy;
	while (*p && i < nwname) {
		if (*p == '/') {
			*p++ = '\0';
			continue;
		}
		wnames[i++] = p;
		while (*p && *p != '/')
			p++;
		if (*p)
			*p++ = '\0';
	}

	// Walk path
	P9Qid* qids = (P9Qid*)malloc(nwname * sizeof(P9Qid));
	if (qids == NULL) {
		free(wnames);
		free(pathCopy);
		return B_NO_MEMORY;
	}

	uint16 nwqid;
	status_t status = WalkPath(fid, newfid, nwname, wnames, &nwqid, qids);

	if (status == B_OK && qid != NULL && nwqid > 0)
		*qid = qids[nwqid - 1];

	free(qids);
	free(wnames);
	free(pathCopy);

	if (status == B_OK && nwqid != nwname)
		return B_ENTRY_NOT_FOUND;

	return status;
}


status_t
P9Client::WalkPath(uint32 fid, uint32 newfid, uint16 nwname,
	const char** wnames, uint16* nwqid, P9Qid* qids)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildWalk(tag, fid, newfid, nwname, wnames);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	uint8 type = response->Type();
	if (type != P9_RWALK) {
		delete response;
		return B_ERROR;
	}

	status = response->ParseWalk(*nwqid, qids, nwname);
	delete response;
	return status;
}


status_t
P9Client::Open(uint32 fid, uint32 flags, P9Qid* qid, uint32* iounit)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildLopen(tag, fid, flags);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RLOPEN) {
		delete response;
		return B_ERROR;
	}

	P9Qid respQid;
	uint32 respIounit;
	status = response->ParseLopen(respQid, respIounit);
	delete response;

	if (status == B_OK) {
		if (qid != NULL)
			*qid = respQid;
		if (iounit != NULL)
			*iounit = respIounit > 0 ? respIounit : fIOUnit;
	}

	return status;
}


status_t
P9Client::Create(uint32 fid, const char* name, uint32 flags, uint32 mode,
	uint32 gid, P9Qid* qid, uint32* iounit)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildLcreate(tag, fid, name, flags, mode, gid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RLCREATE) {
		delete response;
		return B_ERROR;
	}

	P9Qid respQid;
	uint32 respIounit;
	status = response->ParseLcreate(respQid, respIounit);
	delete response;

	if (status == B_OK) {
		if (qid != NULL)
			*qid = respQid;
		if (iounit != NULL)
			*iounit = respIounit > 0 ? respIounit : fIOUnit;
	}

	return status;
}


status_t
P9Client::Read(uint32 fid, uint64 offset, void* buffer, uint32* count)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint32 toRead = *count;
	if (toRead > fIOUnit)
		toRead = fIOUnit;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildRead(tag, fid, offset, toRead);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RREAD) {
		delete response;
		return B_ERROR;
	}

	void* data;
	status = response->ParseRead(*count, &data);
	if (status == B_OK && *count > 0)
		memcpy(buffer, data, *count);

	delete response;
	return status;
}


status_t
P9Client::Write(uint32 fid, uint64 offset, const void* buffer, uint32* count)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint32 toWrite = *count;
	if (toWrite > fIOUnit)
		toWrite = fIOUnit;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildWrite(tag, fid, offset, buffer, toWrite);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RWRITE) {
		delete response;
		return B_ERROR;
	}

	status = response->ParseWrite(*count);
	delete response;
	return status;
}


status_t
P9Client::Clunk(uint32 fid)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildClunk(tag, fid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::Remove(uint32 fid)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildRemove(tag, fid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::GetAttr(uint32 fid, uint64 mask, P9Attr* attr)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildGetattr(tag, fid, mask);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RGETATTR) {
		delete response;
		return B_ERROR;
	}

	status = response->ParseGetattr(*attr);
	delete response;
	return status;
}


status_t
P9Client::SetAttr(uint32 fid, uint32 valid, uint32 mode, uint32 uid,
	uint32 gid, uint64 size, uint64 atime_sec, uint64 atime_nsec,
	uint64 mtime_sec, uint64 mtime_nsec)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildSetattr(tag, fid, valid, mode, uid, gid, size,
		atime_sec, atime_nsec, mtime_sec, mtime_nsec);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::ReadDir(uint32 fid, uint64 offset, void* buffer, uint32* count)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint32 toRead = *count;
	if (toRead > fIOUnit)
		toRead = fIOUnit;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildReaddir(tag, fid, offset, toRead);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RREADDIR) {
		delete response;
		return B_ERROR;
	}

	void* data;
	status = response->ParseReaddir(*count, &data);
	if (status == B_OK && *count > 0)
		memcpy(buffer, data, *count);

	delete response;
	return status;
}


status_t
P9Client::Mkdir(uint32 dfid, const char* name, uint32 mode, uint32 gid,
	P9Qid* qid)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildMkdir(tag, dfid, name, mode, gid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RMKDIR) {
		delete response;
		return B_ERROR;
	}

	if (qid != NULL) {
		status = response->ParseMkdir(*qid);
	}

	delete response;
	return status;
}


status_t
P9Client::Unlink(uint32 dfid, const char* name, uint32 flags)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildUnlinkat(tag, dfid, name, flags);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::Rename(uint32 olddirfid, const char* oldname,
	uint32 newdirfid, const char* newname)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildRenameat(tag, olddirfid, oldname, newdirfid, newname);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::StatFS(uint32 fid, P9StatFS* statfs)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildStatfs(tag, fid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RSTATFS) {
		delete response;
		return B_ERROR;
	}

	status = response->ParseStatfs(*statfs);
	delete response;
	return status;
}


status_t
P9Client::FSync(uint32 fid, bool dataOnly)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildFsync(tag, fid, dataOnly ? 1 : 0);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}


status_t
P9Client::ReadLink(uint32 fid, char* target, size_t targetSize)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildReadlink(tag, fid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RREADLINK) {
		delete response;
		return B_ERROR;
	}

	status = response->ParseReadlink(target, targetSize);
	delete response;
	return status;
}


status_t
P9Client::Symlink(uint32 dfid, const char* name, const char* target,
	uint32 gid, P9Qid* qid)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildSymlink(tag, dfid, name, target, gid);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	if (status != B_OK) {
		delete response;
		return status;
	}

	if (response->Type() != P9_RSYMLINK) {
		delete response;
		return B_ERROR;
	}

	if (qid != NULL) {
		status = response->ParseSymlink(*qid);
	}

	delete response;
	return status;
}


status_t
P9Client::Link(uint32 dfid, uint32 fid, const char* name)
{
	P9Message request(fMsize);
	status_t status = request.Init();
	if (status != B_OK)
		return status;

	uint16 tag = fTagPool.Allocate();
	status = request.BuildLink(tag, dfid, fid, name);
	if (status != B_OK) {
		fTagPool.Release(tag);
		return status;
	}

	P9Message* response = NULL;
	status = DoRequest(&request, &response);
	fTagPool.Release(tag);
	if (status != B_OK)
		return status;

	status = CheckError(response);
	delete response;
	return status;
}
