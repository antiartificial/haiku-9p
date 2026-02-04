/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Inode Implementation
 */

#include "Inode.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include <dirent.h>
#include <KernelExport.h>

#include "9p_client.h"
#include "Volume.h"


//#define TRACE_9P_INODE
#ifdef TRACE_9P_INODE
#	define TRACE(x...) dprintf("9p_ino: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("9p_ino: " x)


// Directory read buffer size
#define DIR_BUFFER_SIZE		4096


Inode::Inode(Volume* volume, ino_t id, uint32 fid, const P9Qid& qid)
	:
	fVolume(volume),
	fID(id),
	fFid(fid),
	fQid(qid),
	fMode(0),
	fSize(0),
	fStatValid(false)
{
	mutex_init(&fLock, "9p inode");

	// Set initial mode from qid type
	if (qid.type & P9_QTDIR)
		fMode = S_IFDIR | 0755;
	else if (qid.type & P9_QTSYMLINK)
		fMode = S_IFLNK | 0777;
	else
		fMode = S_IFREG | 0644;
}


Inode::~Inode()
{
	// Clunk fid when inode is destroyed
	if (fFid != fVolume->RootFid() && fFid != P9_NOFID)
		fVolume->Client()->Clunk(fFid);

	mutex_destroy(&fLock);
}


status_t
Inode::Init()
{
	// Get initial attributes
	return _UpdateStat();
}


status_t
Inode::_UpdateStat()
{
	P9Attr attr;
	status_t status = fVolume->Client()->GetAttr(fFid, P9_GETATTR_BASIC, &attr);
	if (status != B_OK)
		return status;

	fMode = p9_mode_to_haiku(attr.mode);
	fSize = attr.size;
	fStatValid = true;

	return B_OK;
}


status_t
Inode::Open(int openMode, void** _cookie)
{
	TRACE("Open(%d)\n", openMode);

	MutexLocker locker(&fLock);

	// Clone fid for this open
	uint32 newFid = fVolume->Client()->AllocateFid();
	if (newFid == P9_NOFID)
		return B_NO_MEMORY;

	status_t status = fVolume->Client()->Walk(fFid, newFid, NULL);
	if (status != B_OK) {
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Convert Haiku open mode to 9P flags
	uint32 flags = haiku_to_p9_open_flags(openMode);

	P9Qid qid;
	uint32 iounit;
	status = fVolume->Client()->Open(newFid, flags, &qid, &iounit);
	if (status != B_OK) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Create cookie
	FileCookie* cookie = new(std::nothrow) FileCookie;
	if (cookie == NULL) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return B_NO_MEMORY;
	}

	cookie->fid = newFid;
	cookie->openMode = openMode;
	cookie->position = 0;

	*_cookie = cookie;
	return B_OK;
}


status_t
Inode::Close(void* _cookie)
{
	FileCookie* cookie = (FileCookie*)_cookie;
	TRACE("Close(fid=%u)\n", cookie->fid);

	// Nothing to do - cleanup in FreeCookie
	return B_OK;
}


status_t
Inode::FreeCookie(void* _cookie)
{
	FileCookie* cookie = (FileCookie*)_cookie;
	TRACE("FreeCookie(fid=%u)\n", cookie->fid);

	fVolume->Client()->Clunk(cookie->fid);
	fVolume->Client()->ReleaseFid(cookie->fid);
	delete cookie;

	return B_OK;
}


status_t
Inode::Read(void* _cookie, off_t pos, void* buffer, size_t* _length)
{
	FileCookie* cookie = (FileCookie*)_cookie;
	TRACE("Read(fid=%u, pos=%lld, len=%zu)\n", cookie->fid, pos, *_length);

	if (pos < 0)
		return B_BAD_VALUE;

	size_t remaining = *_length;
	uint8* dest = (uint8*)buffer;
	size_t totalRead = 0;

	while (remaining > 0) {
		uint32 toRead = remaining > fVolume->Client()->IOUnit()
			? fVolume->Client()->IOUnit() : remaining;

		status_t status = fVolume->Client()->Read(cookie->fid, pos + totalRead,
			dest, &toRead);
		if (status != B_OK)
			return status;

		if (toRead == 0)
			break;	// EOF

		dest += toRead;
		totalRead += toRead;
		remaining -= toRead;
	}

	*_length = totalRead;
	return B_OK;
}


status_t
Inode::Write(void* _cookie, off_t pos, const void* buffer, size_t* _length)
{
	FileCookie* cookie = (FileCookie*)_cookie;
	TRACE("Write(fid=%u, pos=%lld, len=%zu)\n", cookie->fid, pos, *_length);

	if (pos < 0)
		return B_BAD_VALUE;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	size_t remaining = *_length;
	const uint8* src = (const uint8*)buffer;
	size_t totalWritten = 0;

	while (remaining > 0) {
		uint32 toWrite = remaining > fVolume->Client()->IOUnit()
			? fVolume->Client()->IOUnit() : remaining;

		status_t status = fVolume->Client()->Write(cookie->fid,
			pos + totalWritten, src, &toWrite);
		if (status != B_OK)
			return status;

		if (toWrite == 0)
			break;

		src += toWrite;
		totalWritten += toWrite;
		remaining -= toWrite;
	}

	*_length = totalWritten;

	// Invalidate cached stat
	fStatValid = false;

	return B_OK;
}


status_t
Inode::ReadStat(struct stat* stat)
{
	TRACE("ReadStat()\n");

	MutexLocker locker(&fLock);

	P9Attr attr;
	status_t status = fVolume->Client()->GetAttr(fFid, P9_GETATTR_ALL, &attr);
	if (status != B_OK)
		return status;

	stat->st_dev = fVolume->ID();
	stat->st_ino = fID;
	stat->st_mode = p9_mode_to_haiku(attr.mode);
	stat->st_nlink = attr.nlink;
	stat->st_uid = attr.uid;
	stat->st_gid = attr.gid;
	stat->st_rdev = attr.rdev;
	stat->st_size = attr.size;
	stat->st_blksize = attr.blksize;
	stat->st_blocks = attr.blocks;
	stat->st_atim.tv_sec = attr.atime_sec;
	stat->st_atim.tv_nsec = attr.atime_nsec;
	stat->st_mtim.tv_sec = attr.mtime_sec;
	stat->st_mtim.tv_nsec = attr.mtime_nsec;
	stat->st_ctim.tv_sec = attr.ctime_sec;
	stat->st_ctim.tv_nsec = attr.ctime_nsec;

	// Update cached values
	fMode = stat->st_mode;
	fSize = stat->st_size;
	fStatValid = true;

	return B_OK;
}


status_t
Inode::WriteStat(const struct stat* stat, uint32 statMask)
{
	TRACE("WriteStat(mask=0x%x)\n", statMask);

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	MutexLocker locker(&fLock);

	uint32 valid = 0;
	uint32 mode = 0;
	uint32 uid = 0;
	uint32 gid = 0;
	uint64 size = 0;
	uint64 atime_sec = 0, atime_nsec = 0;
	uint64 mtime_sec = 0, mtime_nsec = 0;

	if (statMask & B_STAT_MODE) {
		valid |= P9_SETATTR_MODE;
		mode = haiku_mode_to_p9(stat->st_mode);
	}

	if (statMask & B_STAT_UID) {
		valid |= P9_SETATTR_UID;
		uid = stat->st_uid;
	}

	if (statMask & B_STAT_GID) {
		valid |= P9_SETATTR_GID;
		gid = stat->st_gid;
	}

	if (statMask & B_STAT_SIZE) {
		valid |= P9_SETATTR_SIZE;
		size = stat->st_size;
	}

	if (statMask & B_STAT_ACCESS_TIME) {
		valid |= P9_SETATTR_ATIME | P9_SETATTR_ATIME_SET;
		atime_sec = stat->st_atim.tv_sec;
		atime_nsec = stat->st_atim.tv_nsec;
	}

	if (statMask & B_STAT_MODIFICATION_TIME) {
		valid |= P9_SETATTR_MTIME | P9_SETATTR_MTIME_SET;
		mtime_sec = stat->st_mtim.tv_sec;
		mtime_nsec = stat->st_mtim.tv_nsec;
	}

	status_t status = fVolume->Client()->SetAttr(fFid, valid, mode, uid, gid,
		size, atime_sec, atime_nsec, mtime_sec, mtime_nsec);

	if (status == B_OK)
		fStatValid = false;

	return status;
}


status_t
Inode::Lookup(const char* name, ino_t* _id)
{
	TRACE("Lookup(%s)\n", name);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	// Handle . and ..
	if (strcmp(name, ".") == 0) {
		*_id = fID;
		return B_OK;
	}

	// Walk to child
	uint32 childFid;
	P9Qid childQid;
	status_t status = WalkToChild(name, &childFid, &childQid);
	if (status != B_OK)
		return status;

	// Get or create inode
	Inode* inode;
	status = fVolume->GetInode(childFid, childQid, &inode);
	if (status != B_OK) {
		fVolume->Client()->Clunk(childFid);
		fVolume->Client()->ReleaseFid(childFid);
		return status;
	}

	*_id = inode->ID();

	// put_vnode will be called by VFS
	return B_OK;
}


status_t
Inode::Create(const char* name, int openMode, int perms,
	void** _cookie, ino_t* _newID)
{
	TRACE("Create(%s, mode=0x%x, perms=0%o)\n", name, openMode, perms);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	MutexLocker locker(&fLock);

	// Clone directory fid
	uint32 newFid = fVolume->Client()->AllocateFid();
	if (newFid == P9_NOFID)
		return B_NO_MEMORY;

	status_t status = fVolume->Client()->Walk(fFid, newFid, NULL);
	if (status != B_OK) {
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Create file
	uint32 flags = haiku_to_p9_open_flags(openMode);
	uint32 mode = haiku_mode_to_p9(S_IFREG | perms);

	P9Qid qid;
	uint32 iounit;
	status = fVolume->Client()->Create(newFid, name, flags, mode, 0,
		&qid, &iounit);
	if (status != B_OK) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Create inode for new file
	ino_t id = fVolume->QidToIno(qid);

	Inode* inode = new(std::nothrow) Inode(fVolume, id, P9_NOFID, qid);
	if (inode == NULL) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return B_NO_MEMORY;
	}

	status = inode->Init();
	if (status != B_OK) {
		delete inode;
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Publish vnode
	status = publish_vnode(fVolume->FSVolume(), id, inode, &gInodeOps,
		S_IFREG, 0);
	if (status != B_OK) {
		delete inode;
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Create cookie
	FileCookie* cookie = new(std::nothrow) FileCookie;
	if (cookie == NULL) {
		put_vnode(fVolume->FSVolume(), id);
		return B_NO_MEMORY;
	}

	cookie->fid = newFid;
	cookie->openMode = openMode;
	cookie->position = 0;

	*_cookie = cookie;
	*_newID = id;

	return B_OK;
}


status_t
Inode::Remove(const char* name)
{
	TRACE("Remove(%s)\n", name);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	MutexLocker locker(&fLock);

	return fVolume->Client()->Unlink(fFid, name, 0);
}


status_t
Inode::Rename(const char* fromName, Inode* toDir, const char* toName)
{
	TRACE("Rename(%s -> %s)\n", fromName, toName);

	if (!IsDirectory() || !toDir->IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	return fVolume->Client()->Rename(fFid, fromName, toDir->Fid(), toName);
}


status_t
Inode::OpenDir(void** _cookie)
{
	TRACE("OpenDir()\n");

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	MutexLocker locker(&fLock);

	// Clone fid for directory iteration
	uint32 newFid = fVolume->Client()->AllocateFid();
	if (newFid == P9_NOFID)
		return B_NO_MEMORY;

	status_t status = fVolume->Client()->Walk(fFid, newFid, NULL);
	if (status != B_OK) {
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Open directory
	status = fVolume->Client()->Open(newFid, P9_OREAD);
	if (status != B_OK) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return status;
	}

	// Create cookie
	DirCookie* cookie = new(std::nothrow) DirCookie;
	if (cookie == NULL) {
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return B_NO_MEMORY;
	}

	cookie->fid = newFid;
	cookie->offset = 0;
	cookie->buffer = malloc(DIR_BUFFER_SIZE);
	cookie->bufferSize = 0;
	cookie->bufferPos = 0;
	cookie->eof = false;

	if (cookie->buffer == NULL) {
		delete cookie;
		fVolume->Client()->Clunk(newFid);
		fVolume->Client()->ReleaseFid(newFid);
		return B_NO_MEMORY;
	}

	*_cookie = cookie;
	return B_OK;
}


status_t
Inode::CloseDir(void* _cookie)
{
	DirCookie* cookie = (DirCookie*)_cookie;
	TRACE("CloseDir(fid=%u)\n", cookie->fid);

	return B_OK;
}


status_t
Inode::FreeDirCookie(void* _cookie)
{
	DirCookie* cookie = (DirCookie*)_cookie;
	TRACE("FreeDirCookie(fid=%u)\n", cookie->fid);

	fVolume->Client()->Clunk(cookie->fid);
	fVolume->Client()->ReleaseFid(cookie->fid);
	free(cookie->buffer);
	delete cookie;

	return B_OK;
}


status_t
Inode::ReadDir(void* _cookie, struct dirent* buffer, size_t bufferSize,
	uint32* _num)
{
	DirCookie* cookie = (DirCookie*)_cookie;
	TRACE("ReadDir(fid=%u, offset=%llu)\n", cookie->fid, cookie->offset);

	uint32 count = 0;
	uint32 maxCount = *_num;

	while (count < maxCount) {
		// Refill buffer if needed
		if (cookie->bufferPos >= cookie->bufferSize && !cookie->eof) {
			uint32 toRead = DIR_BUFFER_SIZE;
			status_t status = fVolume->Client()->ReadDir(cookie->fid,
				cookie->offset, cookie->buffer, &toRead);
			if (status != B_OK)
				return status;

			cookie->bufferSize = toRead;
			cookie->bufferPos = 0;

			if (toRead == 0) {
				cookie->eof = true;
				break;
			}
		}

		if (cookie->eof)
			break;

		// Parse entries from buffer
		P9DirEntryParser parser((uint8*)cookie->buffer + cookie->bufferPos,
			cookie->bufferSize - cookie->bufferPos);

		while (parser.HasNext() && count < maxCount) {
			P9DirEnt entry;
			status_t status = parser.Next(entry);
			if (status != B_OK)
				break;

			// Calculate dirent size
			size_t nameLen = strlen(entry.name);
			size_t recLen = offsetof(struct dirent, d_name) + nameLen + 1;

			if (recLen > bufferSize) {
				free(entry.name);
				if (count == 0)
					return B_BUFFER_OVERFLOW;
				break;
			}

			// Fill dirent
			buffer->d_dev = fVolume->ID();
			buffer->d_ino = fVolume->QidToIno(entry.qid);
			buffer->d_reclen = recLen;
			strlcpy(buffer->d_name, entry.name, nameLen + 1);

			// Advance to next
			buffer = (struct dirent*)((uint8*)buffer + recLen);
			bufferSize -= recLen;
			count++;

			cookie->offset = entry.offset;
			free(entry.name);
		}

		cookie->bufferPos = cookie->bufferSize;
	}

	*_num = count;
	return B_OK;
}


status_t
Inode::RewindDir(void* _cookie)
{
	DirCookie* cookie = (DirCookie*)_cookie;
	TRACE("RewindDir(fid=%u)\n", cookie->fid);

	cookie->offset = 0;
	cookie->bufferSize = 0;
	cookie->bufferPos = 0;
	cookie->eof = false;

	return B_OK;
}


status_t
Inode::ReadLink(char* buffer, size_t* bufferSize)
{
	TRACE("ReadLink()\n");

	if (!IsSymlink())
		return B_BAD_VALUE;

	return fVolume->Client()->ReadLink(fFid, buffer, *bufferSize);
}


status_t
Inode::CreateSymlink(const char* name, const char* target)
{
	TRACE("CreateSymlink(%s -> %s)\n", name, target);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	return fVolume->Client()->Symlink(fFid, name, target, 0);
}


status_t
Inode::CreateDir(const char* name, int perms)
{
	TRACE("CreateDir(%s, 0%o)\n", name, perms);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	uint32 mode = haiku_mode_to_p9(S_IFDIR | perms);
	return fVolume->Client()->Mkdir(fFid, name, mode, 0);
}


status_t
Inode::RemoveDir(const char* name)
{
	TRACE("RemoveDir(%s)\n", name);

	if (!IsDirectory())
		return B_NOT_A_DIRECTORY;

	if (fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	// AT_REMOVEDIR flag
	return fVolume->Client()->Unlink(fFid, name, 0x200);
}


status_t
Inode::Sync()
{
	TRACE("Sync()\n");

	if (fVolume->IsReadOnly())
		return B_OK;

	return fVolume->Client()->FSync(fFid, false);
}


status_t
Inode::WalkToChild(const char* name, uint32* _childFid, P9Qid* _childQid)
{
	uint32 childFid = fVolume->Client()->AllocateFid();
	if (childFid == P9_NOFID)
		return B_NO_MEMORY;

	P9Qid qid;
	status_t status = fVolume->Client()->Walk(fFid, childFid, name, &qid);
	if (status != B_OK) {
		fVolume->Client()->ReleaseFid(childFid);
		return status;
	}

	*_childFid = childFid;
	*_childQid = qid;
	return B_OK;
}
