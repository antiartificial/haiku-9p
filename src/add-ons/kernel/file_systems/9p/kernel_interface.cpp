/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Filesystem Kernel Interface
 */

#include <fcntl.h>
#include <fs_info.h>
#include <fs_interface.h>
#include <KernelExport.h>

#include <new>

#include "9p.h"
#include "Inode.h"
#include "Volume.h"


//#define TRACE_9P_INTERFACE
#ifdef TRACE_9P_INTERFACE
#	define TRACE(x...) dprintf("9p: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("9p: " x)


// #pragma mark - Helper functions


status_t
p9_error_to_haiku(uint32 error)
{
	// Convert Linux errno to Haiku status_t
	switch (error) {
		case 0:				return B_OK;
		case P9_EPERM:		return B_PERMISSION_DENIED;
		case P9_ENOENT:		return B_ENTRY_NOT_FOUND;
		case P9_EIO:		return B_IO_ERROR;
		case P9_ENXIO:		return B_DEV_NOT_READY;
		case P9_EACCES:		return B_PERMISSION_DENIED;
		case P9_EEXIST:		return B_FILE_EXISTS;
		case P9_EXDEV:		return B_CROSS_DEVICE_LINK;
		case P9_ENODEV:		return B_DEV_NOT_READY;
		case P9_ENOTDIR:	return B_NOT_A_DIRECTORY;
		case P9_EISDIR:		return B_IS_A_DIRECTORY;
		case P9_EINVAL:		return B_BAD_VALUE;
		case P9_ENFILE:		return B_NO_MORE_FDS;
		case P9_EMFILE:		return B_NO_MORE_FDS;
		case P9_ENOSPC:		return B_DEVICE_FULL;
		case P9_ESPIPE:		return B_BAD_VALUE;
		case P9_EROFS:		return B_READ_ONLY_DEVICE;
		case P9_ENAMETOOLONG:	return B_NAME_TOO_LONG;
		case P9_ENOTEMPTY:	return B_DIRECTORY_NOT_EMPTY;
		case P9_ENODATA:	return B_ENTRY_NOT_FOUND;
		case P9_EOVERFLOW:	return B_BUFFER_OVERFLOW;
		case P9_EOPNOTSUPP:	return B_NOT_SUPPORTED;
		default:			return B_ERROR;
	}
}


uint32
haiku_to_p9_open_flags(int flags)
{
	uint32 p9flags = 0;

	switch (flags & O_ACCMODE) {
		case O_RDONLY:
			p9flags = P9_OREAD;
			break;
		case O_WRONLY:
			p9flags = P9_OWRITE;
			break;
		case O_RDWR:
			p9flags = P9_ORDWR;
			break;
	}

	if (flags & O_CREAT)
		p9flags |= P9_OCREATE;
	if (flags & O_EXCL)
		p9flags |= P9_OEXCL;
	if (flags & O_TRUNC)
		p9flags |= P9_OTRUNC;
	if (flags & O_APPEND)
		p9flags |= P9_OAPPEND;

	return p9flags;
}


mode_t
p9_mode_to_haiku(uint32 mode)
{
	// 9P uses Linux mode bits, which are the same as Haiku/POSIX
	return (mode_t)mode;
}


uint32
haiku_mode_to_p9(mode_t mode)
{
	return (uint32)mode;
}


// #pragma mark - Volume operations


static status_t
fs_mount(fs_volume* fsVolume, const char* device, uint32 flags,
	const char* args, ino_t* rootID)
{
	TRACE("mount(device=%s, flags=0x%x)\n", device ? device : "(null)", flags);

	Volume* volume = new(std::nothrow) Volume(fsVolume);
	if (volume == NULL)
		return B_NO_MEMORY;

	status_t status = volume->Mount(device, flags, args, rootID);
	if (status != B_OK) {
		delete volume;
		return status;
	}

	fsVolume->private_volume = volume;
	fsVolume->ops = &gVolumeOps;

	return B_OK;
}


static status_t
fs_unmount(fs_volume* fsVolume)
{
	TRACE("unmount()\n");

	Volume* volume = (Volume*)fsVolume->private_volume;
	status_t status = volume->Unmount();
	delete volume;

	return status;
}


static status_t
fs_read_fs_info(fs_volume* fsVolume, struct fs_info* info)
{
	Volume* volume = (Volume*)fsVolume->private_volume;
	return volume->ReadFSInfo(info);
}


static status_t
fs_write_fs_info(fs_volume* fsVolume, const struct fs_info* info, uint32 mask)
{
	Volume* volume = (Volume*)fsVolume->private_volume;
	return volume->WriteFSInfo(info, mask);
}


static status_t
fs_sync(fs_volume* fsVolume)
{
	Volume* volume = (Volume*)fsVolume->private_volume;
	return volume->Sync();
}


static status_t
fs_get_vnode(fs_volume* fsVolume, ino_t id, fs_vnode* vnode, int* _type,
	uint32* _flags, bool reenter)
{
	TRACE("get_vnode(%lld)\n", id);

	// Walk from root to find this inode
	// For now, we return an error - inodes are created on lookup
	(void)fsVolume;
	(void)vnode;
	(void)_type;
	(void)_flags;
	(void)reenter;
	return B_ENTRY_NOT_FOUND;
}


fs_volume_ops gVolumeOps = {
	fs_unmount,
	fs_read_fs_info,
	fs_write_fs_info,
	fs_sync,
	fs_get_vnode,

	// Index operations (not supported)
	NULL,	// open_index_dir
	NULL,	// close_index_dir
	NULL,	// free_index_dir_cookie
	NULL,	// read_index_dir
	NULL,	// rewind_index_dir
	NULL,	// create_index
	NULL,	// remove_index
	NULL,	// read_index_stat

	// Query operations (not supported)
	NULL,	// open_query
	NULL,	// close_query
	NULL,	// free_query_cookie
	NULL,	// read_query
	NULL,	// rewind_query

	// Capabilities
	NULL,	// all_layers_mounted
	NULL,	// create_sub_vnode
	NULL,	// delete_sub_vnode
};


// #pragma mark - Vnode operations


static status_t
fs_lookup(fs_volume* fsVolume, fs_vnode* dir, const char* name, ino_t* id)
{
	Inode* inode = (Inode*)dir->private_node;
	return inode->Lookup(name, id);
}


static status_t
fs_get_vnode_name(fs_volume* fsVolume, fs_vnode* vnode, char* buffer,
	size_t bufferSize)
{
	// Not implemented - VFS handles this via readdir
	return B_NOT_SUPPORTED;
}


static status_t
fs_put_vnode(fs_volume* fsVolume, fs_vnode* vnode, bool reenter)
{
	TRACE("put_vnode(%lld)\n", ((Inode*)vnode->private_node)->ID());

	Inode* inode = (Inode*)vnode->private_node;
	delete inode;

	return B_OK;
}


static status_t
fs_remove_vnode(fs_volume* fsVolume, fs_vnode* vnode, bool reenter)
{
	TRACE("remove_vnode(%lld)\n", ((Inode*)vnode->private_node)->ID());

	Inode* inode = (Inode*)vnode->private_node;
	Volume* volume = inode->GetVolume();
	volume->RemoveInode(inode);
	delete inode;

	return B_OK;
}


// #pragma mark - File operations


static status_t
fs_open(fs_volume* fsVolume, fs_vnode* vnode, int openMode, void** cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->Open(openMode, cookie);
}


static status_t
fs_close(fs_volume* fsVolume, fs_vnode* vnode, void* cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->Close(cookie);
}


static status_t
fs_free_cookie(fs_volume* fsVolume, fs_vnode* vnode, void* cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->FreeCookie(cookie);
}


static status_t
fs_read(fs_volume* fsVolume, fs_vnode* vnode, void* cookie, off_t pos,
	void* buffer, size_t* length)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->Read(cookie, pos, buffer, length);
}


static status_t
fs_write(fs_volume* fsVolume, fs_vnode* vnode, void* cookie, off_t pos,
	const void* buffer, size_t* length)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->Write(cookie, pos, buffer, length);
}


// #pragma mark - Directory operations


static status_t
fs_create(fs_volume* fsVolume, fs_vnode* dir, const char* name, int openMode,
	int perms, void** cookie, ino_t* newID)
{
	Inode* inode = (Inode*)dir->private_node;
	return inode->Create(name, openMode, perms, cookie, newID);
}


static status_t
fs_unlink(fs_volume* fsVolume, fs_vnode* dir, const char* name)
{
	Inode* inode = (Inode*)dir->private_node;
	return inode->Remove(name);
}


static status_t
fs_rename(fs_volume* fsVolume, fs_vnode* fromDir, const char* fromName,
	fs_vnode* toDir, const char* toName)
{
	Inode* fromInode = (Inode*)fromDir->private_node;
	Inode* toInode = (Inode*)toDir->private_node;
	return fromInode->Rename(fromName, toInode, toName);
}


static status_t
fs_mkdir(fs_volume* fsVolume, fs_vnode* parent, const char* name, int perms)
{
	Inode* inode = (Inode*)parent->private_node;
	return inode->CreateDir(name, perms);
}


static status_t
fs_rmdir(fs_volume* fsVolume, fs_vnode* parent, const char* name)
{
	Inode* inode = (Inode*)parent->private_node;
	return inode->RemoveDir(name);
}


static status_t
fs_open_dir(fs_volume* fsVolume, fs_vnode* vnode, void** cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->OpenDir(cookie);
}


static status_t
fs_close_dir(fs_volume* fsVolume, fs_vnode* vnode, void* cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->CloseDir(cookie);
}


static status_t
fs_free_dir_cookie(fs_volume* fsVolume, fs_vnode* vnode, void* cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->FreeDirCookie(cookie);
}


static status_t
fs_read_dir(fs_volume* fsVolume, fs_vnode* vnode, void* cookie,
	struct dirent* buffer, size_t bufferSize, uint32* num)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->ReadDir(cookie, buffer, bufferSize, num);
}


static status_t
fs_rewind_dir(fs_volume* fsVolume, fs_vnode* vnode, void* cookie)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->RewindDir(cookie);
}


// #pragma mark - Attribute operations


static status_t
fs_read_stat(fs_volume* fsVolume, fs_vnode* vnode, struct stat* stat)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->ReadStat(stat);
}


static status_t
fs_write_stat(fs_volume* fsVolume, fs_vnode* vnode, const struct stat* stat,
	uint32 statMask)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->WriteStat(stat, statMask);
}


// #pragma mark - Symlink operations


static status_t
fs_read_link(fs_volume* fsVolume, fs_vnode* vnode, char* buffer,
	size_t* bufferSize)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->ReadLink(buffer, bufferSize);
}


static status_t
fs_create_symlink(fs_volume* fsVolume, fs_vnode* dir, const char* name,
	const char* target, int mode)
{
	Inode* inode = (Inode*)dir->private_node;
	return inode->CreateSymlink(name, target);
}


// #pragma mark - Special operations


static status_t
fs_fsync(fs_volume* fsVolume, fs_vnode* vnode, bool dataSync)
{
	Inode* inode = (Inode*)vnode->private_node;
	return inode->Sync();
}


fs_vnode_ops gInodeOps = {
	fs_lookup,
	fs_get_vnode_name,
	fs_put_vnode,
	fs_remove_vnode,

	// VM operations (not supported for network FS)
	NULL,	// can_page
	NULL,	// read_pages
	NULL,	// write_pages

	// Asynchronous I/O (not supported)
	NULL,	// io
	NULL,	// cancel_io

	// Cache file access (not supported)
	NULL,	// get_file_map

	// Common operations
	NULL,	// ioctl
	NULL,	// set_flags
	NULL,	// select
	NULL,	// deselect
	fs_fsync,
	fs_read_link,
	fs_create_symlink,
	NULL,	// link
	fs_unlink,
	fs_rename,
	NULL,	// access
	fs_read_stat,
	fs_write_stat,
	NULL,	// preallocate

	// File operations
	fs_create,
	fs_open,
	fs_close,
	fs_free_cookie,
	fs_read,
	fs_write,

	// Directory operations
	fs_mkdir,
	fs_rmdir,
	fs_open_dir,
	fs_close_dir,
	fs_free_dir_cookie,
	fs_read_dir,
	fs_rewind_dir,

	// Attribute directory operations (not supported)
	NULL,	// open_attr_dir
	NULL,	// close_attr_dir
	NULL,	// free_attr_dir_cookie
	NULL,	// read_attr_dir
	NULL,	// rewind_attr_dir

	// Attribute operations (not supported)
	NULL,	// create_attr
	NULL,	// open_attr
	NULL,	// close_attr
	NULL,	// free_attr_cookie
	NULL,	// read_attr
	NULL,	// write_attr
	NULL,	// read_attr_stat
	NULL,	// write_attr_stat
	NULL,	// rename_attr
	NULL,	// remove_attr

	// Special nodes (not supported)
	NULL,	// create_special_node
	NULL,	// get_super_vnode
};


// #pragma mark - Module registration


static status_t
fs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			TRACE("module init\n");
			return B_OK;

		case B_MODULE_UNINIT:
			TRACE("module uninit\n");
			return B_OK;

		default:
			return B_ERROR;
	}
}


static file_system_module_info s9PFileSystem = {
	{
		"file_systems/9p" B_CURRENT_FS_API_VERSION,
		0,
		fs_std_ops,
	},

	"9p",						// short name
	"9P Network Filesystem",	// pretty name
	0,							// DDM flags

	// Scanning (not supported - no block device)
	NULL,	// identify_partition
	NULL,	// scan_partition
	NULL,	// free_identify_partition_cookie
	NULL,	// free_partition_content_cookie

	fs_mount,
};


module_info* modules[] = {
	(module_info*)&s9PFileSystem,
	NULL,
};
