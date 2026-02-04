/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Message Encoding/Decoding
 */
#ifndef _9P_MESSAGE_H
#define _9P_MESSAGE_H

#include "9p.h"
#include <KernelExport.h>

class P9Buffer {
public:
						P9Buffer(void* buffer, size_t capacity);
						~P9Buffer();

	// Reset for new message
	void				Reset();
	void				ResetRead();

	// Buffer info
	void*				Data() const { return fBuffer; }
	size_t				Capacity() const { return fCapacity; }
	size_t				Size() const { return fWritePos; }
	size_t				Remaining() const { return fCapacity - fWritePos; }
	size_t				ReadRemaining() const { return fWritePos - fReadPos; }

	// Set size (for received messages)
	void				SetSize(size_t size) { fWritePos = size; }

	// Write operations (encoding)
	status_t			WriteUint8(uint8 value);
	status_t			WriteUint16(uint16 value);
	status_t			WriteUint32(uint32 value);
	status_t			WriteUint64(uint64 value);
	status_t			WriteString(const char* str);
	status_t			WriteString(const char* str, uint16 len);
	status_t			WriteData(const void* data, uint32 len);
	status_t			WriteQid(const P9Qid& qid);

	// Read operations (decoding)
	status_t			ReadUint8(uint8& value);
	status_t			ReadUint16(uint16& value);
	status_t			ReadUint32(uint32& value);
	status_t			ReadUint64(uint64& value);
	status_t			ReadString(char* buffer, size_t bufferSize, uint16& len);
	status_t			ReadStringAlloc(char** str, uint16& len);
	status_t			ReadData(void* buffer, uint32 len);
	status_t			ReadQid(P9Qid& qid);

	// Skip bytes
	status_t			Skip(size_t bytes);

	// Get current read position
	size_t				ReadPosition() const { return fReadPos; }

private:
	void*				fBuffer;
	size_t				fCapacity;
	size_t				fWritePos;
	size_t				fReadPos;
};


class P9Message {
public:
						P9Message(uint32 msize = P9_DEFAULT_MSIZE);
						~P9Message();

	// Initialize/reset
	status_t			Init();
	void				Reset();

	// Access underlying buffer
	P9Buffer*			Buffer() { return &fBuffer; }
	void*				Data() const { return fData; }
	size_t				Size() const { return fBuffer.Size(); }
	size_t				MaxSize() const { return fMsize; }

	// Header operations
	status_t			WriteHeader(uint8 type, uint16 tag);
	status_t			FinalizeHeader();
	status_t			ReadHeader(uint8& type, uint16& tag, uint32& size);

	// Message type helpers
	uint8				Type() const { return fType; }
	uint16				Tag() const { return fTag; }

	// === Request message builders ===

	// Tversion: negotiate protocol version
	status_t			BuildVersion(uint16 tag, uint32 msize,
							const char* version);

	// Tattach: establish connection to filesystem
	status_t			BuildAttach(uint16 tag, uint32 fid, uint32 afid,
							const char* uname, const char* aname,
							uint32 n_uname);

	// Twalk: traverse directory tree
	status_t			BuildWalk(uint16 tag, uint32 fid, uint32 newfid,
							uint16 nwname, const char** wnames);

	// Tlopen: open file (9P2000.L)
	status_t			BuildLopen(uint16 tag, uint32 fid, uint32 flags);

	// Tlcreate: create file (9P2000.L)
	status_t			BuildLcreate(uint16 tag, uint32 fid, const char* name,
							uint32 flags, uint32 mode, uint32 gid);

	// Tread: read from file
	status_t			BuildRead(uint16 tag, uint32 fid, uint64 offset,
							uint32 count);

	// Twrite: write to file
	status_t			BuildWrite(uint16 tag, uint32 fid, uint64 offset,
							const void* data, uint32 count);

	// Tclunk: release fid
	status_t			BuildClunk(uint16 tag, uint32 fid);

	// Tremove: remove file
	status_t			BuildRemove(uint16 tag, uint32 fid);

	// Tgetattr: get file attributes (9P2000.L)
	status_t			BuildGetattr(uint16 tag, uint32 fid, uint64 mask);

	// Tsetattr: set file attributes (9P2000.L)
	status_t			BuildSetattr(uint16 tag, uint32 fid, uint32 valid,
							uint32 mode, uint32 uid, uint32 gid, uint64 size,
							uint64 atime_sec, uint64 atime_nsec,
							uint64 mtime_sec, uint64 mtime_nsec);

	// Treaddir: read directory entries (9P2000.L)
	status_t			BuildReaddir(uint16 tag, uint32 fid, uint64 offset,
							uint32 count);

	// Tmkdir: create directory (9P2000.L)
	status_t			BuildMkdir(uint16 tag, uint32 dfid, const char* name,
							uint32 mode, uint32 gid);

	// Tunlinkat: remove file or directory (9P2000.L)
	status_t			BuildUnlinkat(uint16 tag, uint32 dfid, const char* name,
							uint32 flags);

	// Trenameat: rename file (9P2000.L)
	status_t			BuildRenameat(uint16 tag, uint32 olddirfid,
							const char* oldname, uint32 newdirfid,
							const char* newname);

	// Tstatfs: get filesystem statistics (9P2000.L)
	status_t			BuildStatfs(uint16 tag, uint32 fid);

	// Tfsync: sync file (9P2000.L)
	status_t			BuildFsync(uint16 tag, uint32 fid, uint32 datasync);

	// Treadlink: read symbolic link (9P2000.L)
	status_t			BuildReadlink(uint16 tag, uint32 fid);

	// Tsymlink: create symbolic link (9P2000.L)
	status_t			BuildSymlink(uint16 tag, uint32 dfid, const char* name,
							const char* target, uint32 gid);

	// Tlink: create hard link (9P2000.L)
	status_t			BuildLink(uint16 tag, uint32 dfid, uint32 fid,
							const char* name);

	// === Response parsers ===

	// Rlerror: parse error response (9P2000.L)
	status_t			ParseLerror(uint32& error);

	// Rversion: parse version response
	status_t			ParseVersion(uint32& msize, char* version,
							size_t versionSize);

	// Rattach: parse attach response
	status_t			ParseAttach(P9Qid& qid);

	// Rwalk: parse walk response
	status_t			ParseWalk(uint16& nwqid, P9Qid* qids, uint16 maxQids);

	// Rlopen: parse lopen response
	status_t			ParseLopen(P9Qid& qid, uint32& iounit);

	// Rlcreate: parse lcreate response
	status_t			ParseLcreate(P9Qid& qid, uint32& iounit);

	// Rread: parse read response (returns data pointer, does not copy)
	status_t			ParseRead(uint32& count, void** data);

	// Rwrite: parse write response
	status_t			ParseWrite(uint32& count);

	// Rgetattr: parse getattr response
	status_t			ParseGetattr(P9Attr& attr);

	// Rreaddir: parse readdir entries
	status_t			ParseReaddir(uint32& count, void** data);

	// Rstatfs: parse statfs response
	status_t			ParseStatfs(P9StatFS& statfs);

	// Rmkdir: parse mkdir response
	status_t			ParseMkdir(P9Qid& qid);

	// Rsymlink: parse symlink response
	status_t			ParseSymlink(P9Qid& qid);

	// Rreadlink: parse readlink response
	status_t			ParseReadlink(char* target, size_t targetSize);

private:
	void*				fData;
	uint32				fMsize;
	P9Buffer			fBuffer;
	uint8				fType;
	uint16				fTag;
	bool				fOwnsBuffer;
};


// Helper to parse directory entries from readdir data
class P9DirEntryParser {
public:
						P9DirEntryParser(void* data, uint32 size);

	bool				HasNext() const;
	status_t			Next(P9DirEnt& entry);

private:
	P9Buffer			fBuffer;
};

#endif // _9P_MESSAGE_H
