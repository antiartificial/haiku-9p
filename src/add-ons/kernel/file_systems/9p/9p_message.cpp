/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Message Encoding/Decoding Implementation
 */

#include "9p_message.h"

#include <stdlib.h>
#include <string.h>
#include <ByteOrder.h>


// #pragma mark - P9Buffer


P9Buffer::P9Buffer(void* buffer, size_t capacity)
	:
	fBuffer(buffer),
	fCapacity(capacity),
	fWritePos(0),
	fReadPos(0)
{
}


P9Buffer::~P9Buffer()
{
}


void
P9Buffer::Reset()
{
	fWritePos = 0;
	fReadPos = 0;
}


void
P9Buffer::ResetRead()
{
	fReadPos = 0;
}


status_t
P9Buffer::WriteUint8(uint8 value)
{
	if (fWritePos + 1 > fCapacity)
		return B_BUFFER_OVERFLOW;

	((uint8*)fBuffer)[fWritePos++] = value;
	return B_OK;
}


status_t
P9Buffer::WriteUint16(uint16 value)
{
	if (fWritePos + 2 > fCapacity)
		return B_BUFFER_OVERFLOW;

	// 9P uses little-endian
	value = B_HOST_TO_LENDIAN_INT16(value);
	memcpy((uint8*)fBuffer + fWritePos, &value, 2);
	fWritePos += 2;
	return B_OK;
}


status_t
P9Buffer::WriteUint32(uint32 value)
{
	if (fWritePos + 4 > fCapacity)
		return B_BUFFER_OVERFLOW;

	value = B_HOST_TO_LENDIAN_INT32(value);
	memcpy((uint8*)fBuffer + fWritePos, &value, 4);
	fWritePos += 4;
	return B_OK;
}


status_t
P9Buffer::WriteUint64(uint64 value)
{
	if (fWritePos + 8 > fCapacity)
		return B_BUFFER_OVERFLOW;

	value = B_HOST_TO_LENDIAN_INT64(value);
	memcpy((uint8*)fBuffer + fWritePos, &value, 8);
	fWritePos += 8;
	return B_OK;
}


status_t
P9Buffer::WriteString(const char* str)
{
	uint16 len = str ? strlen(str) : 0;
	return WriteString(str, len);
}


status_t
P9Buffer::WriteString(const char* str, uint16 len)
{
	if (fWritePos + 2 + len > fCapacity)
		return B_BUFFER_OVERFLOW;

	status_t status = WriteUint16(len);
	if (status != B_OK)
		return status;

	if (len > 0) {
		memcpy((uint8*)fBuffer + fWritePos, str, len);
		fWritePos += len;
	}
	return B_OK;
}


status_t
P9Buffer::WriteData(const void* data, uint32 len)
{
	if (fWritePos + 4 + len > fCapacity)
		return B_BUFFER_OVERFLOW;

	status_t status = WriteUint32(len);
	if (status != B_OK)
		return status;

	if (len > 0) {
		memcpy((uint8*)fBuffer + fWritePos, data, len);
		fWritePos += len;
	}
	return B_OK;
}


status_t
P9Buffer::WriteQid(const P9Qid& qid)
{
	status_t status = WriteUint8(qid.type);
	if (status != B_OK)
		return status;

	status = WriteUint32(qid.version);
	if (status != B_OK)
		return status;

	return WriteUint64(qid.path);
}


status_t
P9Buffer::ReadUint8(uint8& value)
{
	if (fReadPos + 1 > fWritePos)
		return B_BUFFER_OVERFLOW;

	value = ((uint8*)fBuffer)[fReadPos++];
	return B_OK;
}


status_t
P9Buffer::ReadUint16(uint16& value)
{
	if (fReadPos + 2 > fWritePos)
		return B_BUFFER_OVERFLOW;

	memcpy(&value, (uint8*)fBuffer + fReadPos, 2);
	value = B_LENDIAN_TO_HOST_INT16(value);
	fReadPos += 2;
	return B_OK;
}


status_t
P9Buffer::ReadUint32(uint32& value)
{
	if (fReadPos + 4 > fWritePos)
		return B_BUFFER_OVERFLOW;

	memcpy(&value, (uint8*)fBuffer + fReadPos, 4);
	value = B_LENDIAN_TO_HOST_INT32(value);
	fReadPos += 4;
	return B_OK;
}


status_t
P9Buffer::ReadUint64(uint64& value)
{
	if (fReadPos + 8 > fWritePos)
		return B_BUFFER_OVERFLOW;

	memcpy(&value, (uint8*)fBuffer + fReadPos, 8);
	value = B_LENDIAN_TO_HOST_INT64(value);
	fReadPos += 8;
	return B_OK;
}


status_t
P9Buffer::ReadString(char* buffer, size_t bufferSize, uint16& len)
{
	status_t status = ReadUint16(len);
	if (status != B_OK)
		return status;

	if (fReadPos + len > fWritePos)
		return B_BUFFER_OVERFLOW;

	if (len >= bufferSize)
		return B_NAME_TOO_LONG;

	memcpy(buffer, (uint8*)fBuffer + fReadPos, len);
	buffer[len] = '\0';
	fReadPos += len;
	return B_OK;
}


status_t
P9Buffer::ReadStringAlloc(char** str, uint16& len)
{
	status_t status = ReadUint16(len);
	if (status != B_OK)
		return status;

	if (fReadPos + len > fWritePos)
		return B_BUFFER_OVERFLOW;

	*str = (char*)malloc(len + 1);
	if (*str == NULL)
		return B_NO_MEMORY;

	memcpy(*str, (uint8*)fBuffer + fReadPos, len);
	(*str)[len] = '\0';
	fReadPos += len;
	return B_OK;
}


status_t
P9Buffer::ReadData(void* buffer, uint32 len)
{
	if (fReadPos + len > fWritePos)
		return B_BUFFER_OVERFLOW;

	memcpy(buffer, (uint8*)fBuffer + fReadPos, len);
	fReadPos += len;
	return B_OK;
}


status_t
P9Buffer::ReadQid(P9Qid& qid)
{
	status_t status = ReadUint8(qid.type);
	if (status != B_OK)
		return status;

	status = ReadUint32(qid.version);
	if (status != B_OK)
		return status;

	return ReadUint64(qid.path);
}


status_t
P9Buffer::Skip(size_t bytes)
{
	if (fReadPos + bytes > fWritePos)
		return B_BUFFER_OVERFLOW;

	fReadPos += bytes;
	return B_OK;
}


// #pragma mark - P9Message


P9Message::P9Message(uint32 msize)
	:
	fData(NULL),
	fMsize(msize),
	fBuffer(NULL, 0),
	fType(0),
	fTag(0),
	fOwnsBuffer(false)
{
}


P9Message::~P9Message()
{
	if (fOwnsBuffer && fData != NULL)
		free(fData);
}


status_t
P9Message::Init()
{
	if (fData == NULL) {
		fData = malloc(fMsize);
		if (fData == NULL)
			return B_NO_MEMORY;
		fOwnsBuffer = true;
	}

	fBuffer = P9Buffer(fData, fMsize);
	return B_OK;
}


void
P9Message::Reset()
{
	fBuffer.Reset();
	fType = 0;
	fTag = 0;
}


status_t
P9Message::WriteHeader(uint8 type, uint16 tag)
{
	fType = type;
	fTag = tag;

	// Reserve space for size (will be filled in FinalizeHeader)
	status_t status = fBuffer.WriteUint32(0);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint8(type);
	if (status != B_OK)
		return status;

	return fBuffer.WriteUint16(tag);
}


status_t
P9Message::FinalizeHeader()
{
	uint32 size = B_HOST_TO_LENDIAN_INT32(fBuffer.Size());
	memcpy(fData, &size, 4);
	return B_OK;
}


status_t
P9Message::ReadHeader(uint8& type, uint16& tag, uint32& size)
{
	fBuffer.ResetRead();

	status_t status = fBuffer.ReadUint32(size);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint8(type);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint16(tag);
	if (status != B_OK)
		return status;

	fType = type;
	fTag = tag;
	return B_OK;
}


// #pragma mark - Request builders


status_t
P9Message::BuildVersion(uint16 tag, uint32 msize, const char* version)
{
	Reset();

	status_t status = WriteHeader(P9_TVERSION, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(msize);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(version);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildAttach(uint16 tag, uint32 fid, uint32 afid,
	const char* uname, const char* aname, uint32 n_uname)
{
	Reset();

	status_t status = WriteHeader(P9_TATTACH, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(afid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(uname);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(aname);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(n_uname);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildWalk(uint16 tag, uint32 fid, uint32 newfid,
	uint16 nwname, const char** wnames)
{
	Reset();

	status_t status = WriteHeader(P9_TWALK, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(newfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint16(nwname);
	if (status != B_OK)
		return status;

	for (uint16 i = 0; i < nwname; i++) {
		status = fBuffer.WriteString(wnames[i]);
		if (status != B_OK)
			return status;
	}

	return FinalizeHeader();
}


status_t
P9Message::BuildLopen(uint16 tag, uint32 fid, uint32 flags)
{
	Reset();

	status_t status = WriteHeader(P9_TLOPEN, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(flags);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildLcreate(uint16 tag, uint32 fid, const char* name,
	uint32 flags, uint32 mode, uint32 gid)
{
	Reset();

	status_t status = WriteHeader(P9_TLCREATE, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(name);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(flags);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(mode);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(gid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildRead(uint16 tag, uint32 fid, uint64 offset, uint32 count)
{
	Reset();

	status_t status = WriteHeader(P9_TREAD, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(offset);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(count);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildWrite(uint16 tag, uint32 fid, uint64 offset,
	const void* data, uint32 count)
{
	Reset();

	status_t status = WriteHeader(P9_TWRITE, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(offset);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(count);
	if (status != B_OK)
		return status;

	if (count > 0) {
		if (fBuffer.Size() + count > fBuffer.Capacity())
			return B_BUFFER_OVERFLOW;
		memcpy((uint8*)fData + fBuffer.Size(), data, count);
		// Manually advance write position
		for (uint32 i = 0; i < count; i++)
			fBuffer.WriteUint8(((uint8*)data)[i]);
	}

	return FinalizeHeader();
}


status_t
P9Message::BuildClunk(uint16 tag, uint32 fid)
{
	Reset();

	status_t status = WriteHeader(P9_TCLUNK, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildRemove(uint16 tag, uint32 fid)
{
	Reset();

	status_t status = WriteHeader(P9_TREMOVE, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildGetattr(uint16 tag, uint32 fid, uint64 mask)
{
	Reset();

	status_t status = WriteHeader(P9_TGETATTR, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(mask);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildSetattr(uint16 tag, uint32 fid, uint32 valid,
	uint32 mode, uint32 uid, uint32 gid, uint64 size,
	uint64 atime_sec, uint64 atime_nsec,
	uint64 mtime_sec, uint64 mtime_nsec)
{
	Reset();

	status_t status = WriteHeader(P9_TSETATTR, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(valid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(mode);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(uid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(gid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(size);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(atime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(atime_nsec);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(mtime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(mtime_nsec);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildReaddir(uint16 tag, uint32 fid, uint64 offset, uint32 count)
{
	Reset();

	status_t status = WriteHeader(P9_TREADDIR, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint64(offset);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(count);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildMkdir(uint16 tag, uint32 dfid, const char* name,
	uint32 mode, uint32 gid)
{
	Reset();

	status_t status = WriteHeader(P9_TMKDIR, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(dfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(name);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(mode);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(gid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildUnlinkat(uint16 tag, uint32 dfid, const char* name,
	uint32 flags)
{
	Reset();

	status_t status = WriteHeader(P9_TUNLINKAT, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(dfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(name);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(flags);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildRenameat(uint16 tag, uint32 olddirfid, const char* oldname,
	uint32 newdirfid, const char* newname)
{
	Reset();

	status_t status = WriteHeader(P9_TRENAMEAT, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(olddirfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(oldname);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(newdirfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(newname);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildStatfs(uint16 tag, uint32 fid)
{
	Reset();

	status_t status = WriteHeader(P9_TSTATFS, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildFsync(uint16 tag, uint32 fid, uint32 datasync)
{
	Reset();

	status_t status = WriteHeader(P9_TFSYNC, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(datasync);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildReadlink(uint16 tag, uint32 fid)
{
	Reset();

	status_t status = WriteHeader(P9_TREADLINK, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildSymlink(uint16 tag, uint32 dfid, const char* name,
	const char* target, uint32 gid)
{
	Reset();

	status_t status = WriteHeader(P9_TSYMLINK, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(dfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(name);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(target);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(gid);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


status_t
P9Message::BuildLink(uint16 tag, uint32 dfid, uint32 fid, const char* name)
{
	Reset();

	status_t status = WriteHeader(P9_TLINK, tag);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(dfid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteUint32(fid);
	if (status != B_OK)
		return status;

	status = fBuffer.WriteString(name);
	if (status != B_OK)
		return status;

	return FinalizeHeader();
}


// #pragma mark - Response parsers


status_t
P9Message::ParseLerror(uint32& error)
{
	return fBuffer.ReadUint32(error);
}


status_t
P9Message::ParseVersion(uint32& msize, char* version, size_t versionSize)
{
	status_t status = fBuffer.ReadUint32(msize);
	if (status != B_OK)
		return status;

	uint16 len;
	return fBuffer.ReadString(version, versionSize, len);
}


status_t
P9Message::ParseAttach(P9Qid& qid)
{
	return fBuffer.ReadQid(qid);
}


status_t
P9Message::ParseWalk(uint16& nwqid, P9Qid* qids, uint16 maxQids)
{
	status_t status = fBuffer.ReadUint16(nwqid);
	if (status != B_OK)
		return status;

	if (nwqid > maxQids)
		return B_BUFFER_OVERFLOW;

	for (uint16 i = 0; i < nwqid; i++) {
		status = fBuffer.ReadQid(qids[i]);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


status_t
P9Message::ParseLopen(P9Qid& qid, uint32& iounit)
{
	status_t status = fBuffer.ReadQid(qid);
	if (status != B_OK)
		return status;

	return fBuffer.ReadUint32(iounit);
}


status_t
P9Message::ParseLcreate(P9Qid& qid, uint32& iounit)
{
	status_t status = fBuffer.ReadQid(qid);
	if (status != B_OK)
		return status;

	return fBuffer.ReadUint32(iounit);
}


status_t
P9Message::ParseRead(uint32& count, void** data)
{
	status_t status = fBuffer.ReadUint32(count);
	if (status != B_OK)
		return status;

	// Point to data in buffer (no copy)
	*data = (uint8*)fData + fBuffer.ReadPosition();
	return B_OK;
}


status_t
P9Message::ParseWrite(uint32& count)
{
	return fBuffer.ReadUint32(count);
}


status_t
P9Message::ParseGetattr(P9Attr& attr)
{
	status_t status = fBuffer.ReadUint64(attr.valid);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadQid(attr.qid);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint32(attr.mode);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint32(attr.uid);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint32(attr.gid);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.nlink);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.rdev);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.size);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.blksize);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.blocks);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.atime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.atime_nsec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.mtime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.mtime_nsec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.ctime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.ctime_nsec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.btime_sec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.btime_nsec);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(attr.gen);
	if (status != B_OK)
		return status;

	return fBuffer.ReadUint64(attr.data_version);
}


status_t
P9Message::ParseReaddir(uint32& count, void** data)
{
	status_t status = fBuffer.ReadUint32(count);
	if (status != B_OK)
		return status;

	*data = (uint8*)fData + fBuffer.ReadPosition();
	return B_OK;
}


status_t
P9Message::ParseStatfs(P9StatFS& statfs)
{
	status_t status = fBuffer.ReadUint32(statfs.type);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint32(statfs.bsize);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.blocks);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.bfree);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.bavail);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.files);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.ffree);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(statfs.fsid);
	if (status != B_OK)
		return status;

	return fBuffer.ReadUint32(statfs.namelen);
}


status_t
P9Message::ParseMkdir(P9Qid& qid)
{
	return fBuffer.ReadQid(qid);
}


status_t
P9Message::ParseSymlink(P9Qid& qid)
{
	return fBuffer.ReadQid(qid);
}


status_t
P9Message::ParseReadlink(char* target, size_t targetSize)
{
	uint16 len;
	return fBuffer.ReadString(target, targetSize, len);
}


// #pragma mark - P9DirEntryParser


P9DirEntryParser::P9DirEntryParser(void* data, uint32 size)
	:
	fBuffer(data, size)
{
	fBuffer.SetSize(size);
}


bool
P9DirEntryParser::HasNext() const
{
	return fBuffer.ReadRemaining() > 0;
}


status_t
P9DirEntryParser::Next(P9DirEnt& entry)
{
	status_t status = fBuffer.ReadQid(entry.qid);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint64(entry.offset);
	if (status != B_OK)
		return status;

	status = fBuffer.ReadUint8(entry.type);
	if (status != B_OK)
		return status;

	uint16 len;
	status = fBuffer.ReadStringAlloc(&entry.name, len);
	if (status != B_OK)
		return status;

	return B_OK;
}
