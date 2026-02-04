/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Inode Implementation
 */
#ifndef _INODE_H
#define _INODE_H

#include <fs_interface.h>
#include <lock.h>

#include "9p.h"

class Volume;


class Inode {
public:
						Inode(Volume* volume, ino_t id, uint32 fid,
							const P9Qid& qid);
						~Inode();

	status_t			Init();

	// Accessors
	Volume*				GetVolume() const { return fVolume; }
	ino_t				ID() const { return fID; }
	uint32				Fid() const { return fFid; }
	const P9Qid&		Qid() const { return fQid; }
	mode_t				Mode() const { return fMode; }

	bool				IsDirectory() const { return S_ISDIR(fMode); }
	bool				IsFile() const { return S_ISREG(fMode); }
	bool				IsSymlink() const { return S_ISLNK(fMode); }

	// File operations
	status_t			Open(int openMode, void** cookie);
	status_t			Close(void* cookie);
	status_t			FreeCookie(void* cookie);
	status_t			Read(void* cookie, off_t pos, void* buffer,
							size_t* length);
	status_t			Write(void* cookie, off_t pos, const void* buffer,
							size_t* length);

	// Stat
	status_t			ReadStat(struct stat* stat);
	status_t			WriteStat(const struct stat* stat, uint32 statMask);

	// Directory operations
	status_t			Lookup(const char* name, ino_t* id);
	status_t			Create(const char* name, int openMode, int perms,
							void** cookie, ino_t* newID);
	status_t			Remove(const char* name);
	status_t			Rename(const char* fromName, Inode* toDir,
							const char* toName);

	// Directory iteration
	status_t			OpenDir(void** cookie);
	status_t			CloseDir(void* cookie);
	status_t			FreeDirCookie(void* cookie);
	status_t			ReadDir(void* cookie, struct dirent* buffer,
							size_t bufferSize, uint32* num);
	status_t			RewindDir(void* cookie);

	// Special operations
	status_t			ReadLink(char* buffer, size_t* bufferSize);
	status_t			CreateSymlink(const char* name, const char* target);
	status_t			CreateDir(const char* name, int perms);
	status_t			RemoveDir(const char* name);

	// Sync
	status_t			Sync();

	// FID management
	status_t			WalkToChild(const char* name, uint32* childFid,
							P9Qid* childQid);

private:
	status_t			_UpdateStat();

	Volume*				fVolume;
	ino_t				fID;
	uint32				fFid;
	P9Qid				fQid;
	mode_t				fMode;
	off_t				fSize;

	mutex				fLock;
	bool				fStatValid;
};


// Open file cookie
struct FileCookie {
	uint32				fid;
	int					openMode;
	off_t				position;
};


// Directory iteration cookie
struct DirCookie {
	uint32				fid;
	uint64				offset;
	void*				buffer;
	uint32				bufferSize;
	uint32				bufferPos;
	bool				eof;
};


// VFS operations
extern fs_vnode_ops gInodeOps;

#endif // _INODE_H
