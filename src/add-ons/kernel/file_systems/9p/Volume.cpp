/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Volume Implementation
 */

#include "Volume.h"

#include <stdlib.h>
#include <string.h>
#include <new>

#include <KernelExport.h>
#include <NodeMonitor.h>

#include "Inode.h"
#include "virtio_9p_device.h"


//#define TRACE_9P_VOLUME
#ifdef TRACE_9P_VOLUME
#	define TRACE(x...) dprintf("9p_vol: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("9p_vol: " x)


Volume::Volume(fs_volume* fsVolume)
	:
	fFSVolume(fsVolume),
	fTransport(NULL),
	fRootFid(P9_NOFID),
	fRootInode(NULL),
	fMountTag(NULL),
	fAname(NULL),
	fReadOnly(false)
{
	mutex_init(&fLock, "9p volume");
}


Volume::~Volume()
{
	free(fMountTag);
	free(fAname);
	mutex_destroy(&fLock);
}


status_t
Volume::Mount(const char* device, uint32 flags, const char* args,
	ino_t* rootID)
{
	TRACE("Mount(device=%s, flags=0x%x, args=%s)\n",
		device ? device : "(null)", flags, args ? args : "(null)");

	// Parse mount arguments
	status_t status = _ParseArgs(args);
	if (status != B_OK)
		return status;

	fReadOnly = (flags & B_MOUNT_READ_ONLY) != 0;

	// Find virtio-9p transport
	status = _FindTransport();
	if (status != B_OK) {
		ERROR("failed to find virtio-9p transport\n");
		return status;
	}

	// Initialize 9P client
	status = fClient.Init(fTransport);
	if (status != B_OK) {
		ERROR("failed to init 9P client: %s\n", strerror(status));
		return status;
	}

	// Connect to 9P server
	status = fClient.Connect(fAname ? fAname : "");
	if (status != B_OK) {
		ERROR("failed to connect: %s\n", strerror(status));
		return status;
	}

	fRootFid = fClient.RootFid();

	// Get root attributes
	P9Attr attr;
	status = fClient.GetAttr(fRootFid, P9_GETATTR_BASIC, &attr);
	if (status != B_OK) {
		ERROR("failed to get root attributes: %s\n", strerror(status));
		fClient.Disconnect();
		return status;
	}

	// Create root inode
	status = GetInode(fRootFid, attr.qid, &fRootInode);
	if (status != B_OK) {
		ERROR("failed to create root inode: %s\n", strerror(status));
		fClient.Disconnect();
		return status;
	}

	*rootID = fRootInode->ID();

	TRACE("mounted, root inode = %lld\n", *rootID);
	return B_OK;
}


status_t
Volume::Unmount()
{
	TRACE("Unmount()\n");

	fClient.Disconnect();

	return B_OK;
}


status_t
Volume::GetInode(ino_t id, Inode** _inode)
{
	// Look up in VFS cache
	void* node;
	status_t status = get_vnode(fFSVolume, id, &node);
	if (status != B_OK)
		return status;

	*_inode = (Inode*)node;
	return B_OK;
}


status_t
Volume::GetInode(uint32 fid, const P9Qid& qid, Inode** _inode)
{
	ino_t id = QidToIno(qid);

	// Try to get existing vnode
	void* node;
	status_t status = get_vnode(fFSVolume, id, &node);
	if (status == B_OK) {
		*_inode = (Inode*)node;
		// Release the new fid since we already have one
		if (fid != fRootFid)
			fClient.Clunk(fid);
		return B_OK;
	}

	// Create new inode
	Inode* inode = new(std::nothrow) Inode(this, id, fid, qid);
	if (inode == NULL) {
		if (fid != fRootFid)
			fClient.Clunk(fid);
		return B_NO_MEMORY;
	}

	status = inode->Init();
	if (status != B_OK) {
		delete inode;
		return status;
	}

	// Publish vnode
	status = publish_vnode(fFSVolume, id, inode, &gInodeOps,
		inode->Mode() & S_IFMT, 0);
	if (status != B_OK) {
		delete inode;
		return status;
	}

	*_inode = inode;
	return B_OK;
}


void
Volume::RemoveInode(Inode* inode)
{
	// Called when vnode is removed
}


ino_t
Volume::QidToIno(const P9Qid& qid) const
{
	// Use qid.path as inode number
	return (ino_t)qid.path;
}


status_t
Volume::ReadFSInfo(struct fs_info* info)
{
	P9StatFS statfs;
	status_t status = fClient.StatFS(fRootFid, &statfs);
	if (status != B_OK)
		return status;

	info->flags = B_FS_IS_PERSISTENT | B_FS_HAS_MIME | B_FS_HAS_ATTR;
	if (fReadOnly)
		info->flags |= B_FS_IS_READONLY;

	info->block_size = statfs.bsize;
	info->io_size = fClient.IOUnit();
	info->total_blocks = statfs.blocks;
	info->free_blocks = statfs.bfree;
	info->total_nodes = statfs.files;
	info->free_nodes = statfs.ffree;

	strlcpy(info->volume_name, fMountTag ? fMountTag : "9p",
		sizeof(info->volume_name));
	strlcpy(info->fsh_name, "9p", sizeof(info->fsh_name));

	return B_OK;
}


status_t
Volume::WriteFSInfo(const struct fs_info* info, uint32 mask)
{
	// Read-only filesystem info
	return B_NOT_SUPPORTED;
}


status_t
Volume::Sync()
{
	// Nothing to sync at volume level
	return B_OK;
}


status_t
Volume::_ParseArgs(const char* args)
{
	if (args == NULL)
		return B_OK;

	char* argsCopy = strdup(args);
	if (argsCopy == NULL)
		return B_NO_MEMORY;

	// Parse comma-separated options
	char* saveptr;
	char* opt = strtok_r(argsCopy, ",", &saveptr);
	while (opt != NULL) {
		char* value = strchr(opt, '=');
		if (value != NULL)
			*value++ = '\0';

		if (strcmp(opt, P9_MOUNT_OPT_TAG) == 0) {
			free(fMountTag);
			fMountTag = value ? strdup(value) : NULL;
		} else if (strcmp(opt, P9_MOUNT_OPT_ANAME) == 0) {
			free(fAname);
			fAname = value ? strdup(value) : NULL;
		}

		opt = strtok_r(NULL, ",", &saveptr);
	}

	free(argsCopy);
	return B_OK;
}


status_t
Volume::_FindTransport()
{
	if (fMountTag == NULL) {
		ERROR("no mount tag specified (use -o tag=<name>)\n");
		return B_BAD_VALUE;
	}

	TRACE("looking for transport with tag '%s'\n", fMountTag);

	// Find transport in the registry
	fTransport = (Virtio9PTransport*)virtio_9p_find_transport(fMountTag);
	if (fTransport == NULL) {
		ERROR("no virtio-9p device found with tag '%s'\n", fMountTag);
		ERROR("available tags can be seen in syslog after boot\n");
		return B_DEVICE_NOT_FOUND;
	}

	TRACE("found transport for tag '%s'\n", fMountTag);
	return B_OK;
}
