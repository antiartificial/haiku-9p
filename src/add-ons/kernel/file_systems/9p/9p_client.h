/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Protocol Client
 */
#ifndef _9P_CLIENT_H
#define _9P_CLIENT_H

#include "9p.h"
#include "9p_message.h"
#include "transport.h"

#include <lock.h>
#include <util/DoublyLinkedList.h>


// Forward declarations
class P9Client;


// FID pool for managing file identifiers
class FidPool {
public:
						FidPool();
						~FidPool();

	status_t			Init(uint32 maxFids = 256);
	uint32				Allocate();
	void				Release(uint32 fid);
	bool				IsValid(uint32 fid) const;

private:
	uint32*				fBitmap;
	uint32				fMaxFids;
	uint32				fBitmapSize;
	uint32				fNextHint;
	mutex				fLock;
};


// Tag pool for managing transaction tags
class TagPool {
public:
						TagPool();
						~TagPool();

	status_t			Init(uint16 maxTags = 256);
	uint16				Allocate();
	void				Release(uint16 tag);

private:
	uint32*				fBitmap;
	uint16				fMaxTags;
	uint32				fBitmapSize;
	uint16				fNextHint;
	mutex				fLock;
};


// Pending request tracking
class P9Request : public DoublyLinkedListLinkImpl<P9Request> {
public:
						P9Request(uint16 tag);
						~P9Request();

	uint16				Tag() const { return fTag; }
	sem_id				CompletionSem() const { return fCompletionSem; }

	void				SetResponse(P9Message* response);
	P9Message*			Response() { return fResponse; }

	status_t			WaitForResponse(bigtime_t timeout = B_INFINITE_TIMEOUT);

private:
	uint16				fTag;
	sem_id				fCompletionSem;
	P9Message*			fResponse;
};


// 9P protocol client
class P9Client {
public:
						P9Client();
						~P9Client();

	status_t			Init(P9Transport* transport, uint32 msize = P9_DEFAULT_MSIZE);
	void				Uninit();

	// Connection management
	status_t			Connect(const char* aname = "");
	void				Disconnect();
	bool				IsConnected() const { return fConnected; }

	// Protocol info
	uint32				MaxSize() const { return fMsize; }
	uint32				IOUnit() const { return fIOUnit; }
	uint32				RootFid() const { return fRootFid; }

	// FID management
	uint32				AllocateFid() { return fFidPool.Allocate(); }
	void				ReleaseFid(uint32 fid) { fFidPool.Release(fid); }

	// === Protocol operations ===

	// Walk: traverse path and get new fid
	status_t			Walk(uint32 fid, uint32 newfid, const char* path,
							P9Qid* qid = NULL);
	status_t			WalkPath(uint32 fid, uint32 newfid,
							uint16 nwname, const char** wnames,
							uint16* nwqid, P9Qid* qids);

	// Open/Create
	status_t			Open(uint32 fid, uint32 flags, P9Qid* qid = NULL,
							uint32* iounit = NULL);
	status_t			Create(uint32 fid, const char* name, uint32 flags,
							uint32 mode, uint32 gid, P9Qid* qid = NULL,
							uint32* iounit = NULL);

	// Read/Write
	status_t			Read(uint32 fid, uint64 offset, void* buffer,
							uint32* count);
	status_t			Write(uint32 fid, uint64 offset, const void* buffer,
							uint32* count);

	// Close
	status_t			Clunk(uint32 fid);

	// Remove
	status_t			Remove(uint32 fid);

	// Attributes
	status_t			GetAttr(uint32 fid, uint64 mask, P9Attr* attr);
	status_t			SetAttr(uint32 fid, uint32 valid, uint32 mode,
							uint32 uid, uint32 gid, uint64 size,
							uint64 atime_sec, uint64 atime_nsec,
							uint64 mtime_sec, uint64 mtime_nsec);

	// Directory operations
	status_t			ReadDir(uint32 fid, uint64 offset, void* buffer,
							uint32* count);
	status_t			Mkdir(uint32 dfid, const char* name, uint32 mode,
							uint32 gid, P9Qid* qid = NULL);
	status_t			Unlink(uint32 dfid, const char* name, uint32 flags);
	status_t			Rename(uint32 olddirfid, const char* oldname,
							uint32 newdirfid, const char* newname);

	// Filesystem info
	status_t			StatFS(uint32 fid, P9StatFS* statfs);

	// Sync
	status_t			FSync(uint32 fid, bool dataOnly = false);

	// Symlinks
	status_t			ReadLink(uint32 fid, char* target, size_t targetSize);
	status_t			Symlink(uint32 dfid, const char* name,
							const char* target, uint32 gid, P9Qid* qid = NULL);

	// Hard links
	status_t			Link(uint32 dfid, uint32 fid, const char* name);

private:
	status_t			DoRequest(P9Message* request, P9Message** response);
	status_t			CheckError(P9Message* response);

	P9Transport*		fTransport;
	FidPool				fFidPool;
	TagPool				fTagPool;
	uint32				fMsize;
	uint32				fIOUnit;
	uint32				fRootFid;
	bool				fConnected;
	mutex				fRequestLock;

	typedef DoublyLinkedList<P9Request> RequestList;
	RequestList			fPendingRequests;
	mutex				fPendingLock;
};

#endif // _9P_CLIENT_H
