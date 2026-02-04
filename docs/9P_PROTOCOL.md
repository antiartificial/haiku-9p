# 9P2000.L Protocol Reference

This document describes the 9P2000.L protocol implementation for Haiku OS.

## Overview

9P is a network protocol for distributed file systems originally developed for Plan 9 from Bell Labs. The 9P2000.L variant extends the base 9P2000 protocol with Linux-specific features for better POSIX compatibility.

## Protocol Basics

### Message Format

All 9P messages have the following header:

```
size[4] type[1] tag[2] payload[...]
```

- `size`: Total message size including header (little-endian)
- `type`: Message type (odd = request/T-message, even = response/R-message)
- `tag`: Transaction identifier (client-chosen, max 65535)
- `payload`: Type-specific data

### QID (Unique File Identifier)

Every file has a unique QID:

```
type[1] version[4] path[8]
```

- `type`: File type (QTDIR=0x80, QTFILE=0x00, QTSYMLINK=0x02)
- `version`: Incremented on each modification
- `path`: Unique file identifier on the server

### FID (File Handle)

FIDs are client-managed handles to files/directories. They are:
- Allocated by the client
- Bound to files via `attach` or `walk`
- Released via `clunk`

## Core Messages

### Version Negotiation

```
Tversion: size[4] type=100[1] tag=NOTAG[2] msize[4] version[s]
Rversion: size[4] type=101[1] tag=NOTAG[2] msize[4] version[s]
```

- `msize`: Maximum message size (negotiated)
- `version`: Protocol version string ("9P2000.L")

### Attach

Establish initial connection to filesystem:

```
Tattach: size[4] type=104[1] tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4]
Rattach: size[4] type=105[1] tag[2] qid[13]
```

- `fid`: FID to bind to root
- `afid`: Authentication FID (NOFID if none)
- `uname`: User name
- `aname`: File tree name (mount point)
- `n_uname`: Numeric user ID

### Walk

Traverse directory tree:

```
Twalk: size[4] type=110[1] tag[2] fid[4] newfid[4] nwname[2] wname[s]...
Rwalk: size[4] type=111[1] tag[2] nwqid[2] qid[13]...
```

- `fid`: Starting point FID
- `newfid`: New FID to assign (can equal fid to clone)
- `nwname`: Number of path components (0 = clone)
- `wname`: Path component names

### Lopen (9P2000.L)

Open file for I/O:

```
Tlopen: size[4] type=12[1] tag[2] fid[4] flags[4]
Rlopen: size[4] type=13[1] tag[2] qid[13] iounit[4]
```

- `flags`: Linux open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
- `iounit`: Maximum read/write size (0 = use msize)

### Lcreate (9P2000.L)

Create and open file:

```
Tlcreate: size[4] type=14[1] tag[2] fid[4] name[s] flags[4] mode[4] gid[4]
Rlcreate: size[4] type=15[1] tag[2] qid[13] iounit[4]
```

### Read

Read data from file:

```
Tread: size[4] type=116[1] tag[2] fid[4] offset[8] count[4]
Rread: size[4] type=117[1] tag[2] count[4] data[count]
```

### Write

Write data to file:

```
Twrite: size[4] type=118[1] tag[2] fid[4] offset[8] count[4] data[count]
Rwrite: size[4] type=119[1] tag[2] count[4]
```

### Clunk

Release FID:

```
Tclunk: size[4] type=120[1] tag[2] fid[4]
Rclunk: size[4] type=121[1] tag[2]
```

### Getattr (9P2000.L)

Get file attributes:

```
Tgetattr: size[4] type=24[1] tag[2] fid[4] request_mask[8]
Rgetattr: size[4] type=25[1] tag[2] valid[8] qid[13] mode[4] uid[4] gid[4]
          nlink[8] rdev[8] size[8] blksize[8] blocks[8]
          atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
          ctime_sec[8] ctime_nsec[8] btime_sec[8] btime_nsec[8]
          gen[8] data_version[8]
```

### Setattr (9P2000.L)

Set file attributes:

```
Tsetattr: size[4] type=26[1] tag[2] fid[4] valid[4] mode[4] uid[4] gid[4]
          size[8] atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
Rsetattr: size[4] type=27[1] tag[2]
```

### Readdir (9P2000.L)

Read directory entries:

```
Treaddir: size[4] type=40[1] tag[2] fid[4] offset[8] count[4]
Rreaddir: size[4] type=41[1] tag[2] count[4] data[count]
```

Directory entry format:
```
qid[13] offset[8] type[1] name[s]
```

### Mkdir (9P2000.L)

Create directory:

```
Tmkdir: size[4] type=72[1] tag[2] dfid[4] name[s] mode[4] gid[4]
Rmkdir: size[4] type=73[1] tag[2] qid[13]
```

### Unlinkat (9P2000.L)

Remove file or directory:

```
Tunlinkat: size[4] type=76[1] tag[2] dfid[4] name[s] flags[4]
Runlinkat: size[4] type=77[1] tag[2]
```

- `flags`: 0 for file, AT_REMOVEDIR (0x200) for directory

### Renameat (9P2000.L)

Rename file:

```
Trenameat: size[4] type=74[1] tag[2] olddirfid[4] oldname[s] newdirfid[4] newname[s]
Rrenameat: size[4] type=75[1] tag[2]
```

### Symlink (9P2000.L)

Create symbolic link:

```
Tsymlink: size[4] type=16[1] tag[2] dfid[4] name[s] symtgt[s] gid[4]
Rsymlink: size[4] type=17[1] tag[2] qid[13]
```

### Readlink (9P2000.L)

Read symbolic link target:

```
Treadlink: size[4] type=22[1] tag[2] fid[4]
Rreadlink: size[4] type=23[1] tag[2] target[s]
```

### Statfs (9P2000.L)

Get filesystem statistics:

```
Tstatfs: size[4] type=8[1] tag[2] fid[4]
Rstatfs: size[4] type=9[1] tag[2] type[4] bsize[4] blocks[8] bfree[8]
         bavail[8] files[8] ffree[8] fsid[8] namelen[4]
```

### Fsync (9P2000.L)

Synchronize file:

```
Tfsync: size[4] type=50[1] tag[2] fid[4] datasync[4]
Rfsync: size[4] type=51[1] tag[2]
```

## Error Handling

9P2000.L uses Rlerror for all errors:

```
Rlerror: size[4] type=7[1] tag[2] ecode[4]
```

`ecode` is a Linux errno value.

## Virtio Transport

The 9P protocol runs over virtio-9p transport in QEMU:

- Device type: 9 (VIRTIO_ID_9P)
- Single virtqueue for requests
- Configuration space contains mount tag
- Each request uses two scatter-gather entries:
  1. Request buffer (device read)
  2. Response buffer (device write)

## QEMU Usage

Start QEMU with 9P filesystem sharing:

```bash
qemu-system-x86_64 \
  -virtfs local,path=/shared,mount_tag=host,security_model=mapped-xattr
```

Mount in Haiku:

```bash
mount -t 9p -o tag=host /mnt/host
```

## References

- [9P2000.L Linux kernel documentation](https://github.com/torvalds/linux/blob/master/Documentation/filesystems/9p.rst)
- [Plan 9 Manual - intro(5)](http://man.cat-v.org/plan_9/5/intro)
- [Linux v9fs source code](https://github.com/torvalds/linux/tree/master/fs/9p)
- [QEMU virtio-9p source code](https://github.com/qemu/qemu/tree/master/hw/9pfs)
