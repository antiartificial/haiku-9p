/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Volume Implementation
 */
#ifndef _VOLUME_H
#define _VOLUME_H

#include <fs_interface.h>
#include <lock.h>

#include "9p.h"
#include "9p_client.h"

class Inode;
class Virtio9PTransport;


class Volume {
public:
						Volume(fs_volume* fsVolume);
						~Volume();

	status_t			Mount(const char* device, uint32 flags,
							const char* args, ino_t* rootID);
	status_t			Unmount();

	// Accessors
	fs_volume*			FSVolume() const { return fFSVolume; }
	dev_t				ID() const { return fFSVolume->id; }
	P9Client*			Client() { return &fClient; }
	uint32				RootFid() const { return fRootFid; }

	// Inode management
	status_t			GetInode(ino_t id, Inode** inode);
	status_t			GetInode(uint32 fid, const P9Qid& qid, Inode** inode);
	void				RemoveInode(Inode* inode);

	// Generate inode ID from qid
	ino_t				QidToIno(const P9Qid& qid) const;

	// File system info
	status_t			ReadFSInfo(struct fs_info* info);
	status_t			WriteFSInfo(const struct fs_info* info, uint32 mask);
	status_t			Sync();

	// Volume properties
	bool				IsReadOnly() const { return fReadOnly; }
	const char*			MountTag() const { return fMountTag; }

private:
	status_t			_ParseArgs(const char* args);
	status_t			_FindTransport();

	fs_volume*			fFSVolume;
	P9Client			fClient;
	Virtio9PTransport*	fTransport;

	uint32				fRootFid;
	Inode*				fRootInode;

	char*				fMountTag;
	char*				fAname;
	bool				fReadOnly;

	mutex				fLock;
};


// Mount arguments
#define P9_MOUNT_OPT_TAG		"tag"
#define P9_MOUNT_OPT_ANAME		"aname"
#define P9_MOUNT_OPT_MSIZE		"msize"

// VFS operations
extern fs_volume_ops gVolumeOps;

#endif // _VOLUME_H
