/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P2000.L Protocol Definitions
 */
#ifndef _9P_H
#define _9P_H

#include <SupportDefs.h>

// 9P protocol version strings
#define P9_VERSION_9P2000	"9P2000"
#define P9_VERSION_9P2000_L	"9P2000.L"
#define P9_VERSION_9P2000_U	"9P2000.u"

// Default values
#define P9_DEFAULT_MSIZE	8192
#define P9_MAX_MSIZE		65536
#define P9_NOTAG			((uint16)~0)
#define P9_NOFID			((uint32)~0)
#define P9_NONUNAME			((uint32)~0)

// 9P message types (T = request, R = response)
enum P9MessageType {
	// 9P2000 base protocol
	P9_TLERROR		= 6,	// not used in 9P2000.L
	P9_RLERROR		= 7,
	P9_TSTATFS		= 8,
	P9_RSTATFS		= 9,
	P9_TLOPEN		= 12,
	P9_RLOPEN		= 13,
	P9_TLCREATE		= 14,
	P9_RLCREATE		= 15,
	P9_TSYMLINK		= 16,
	P9_RSYMLINK		= 17,
	P9_TMKNOD		= 18,
	P9_RMKNOD		= 19,
	P9_TRENAME		= 20,
	P9_RRENAME		= 21,
	P9_TREADLINK	= 22,
	P9_RREADLINK	= 23,
	P9_TGETATTR		= 24,
	P9_RGETATTR		= 25,
	P9_TSETATTR		= 26,
	P9_RSETATTR		= 27,
	P9_TXATTRWALK	= 30,
	P9_RXATTRWALK	= 31,
	P9_TXATTRCREATE	= 32,
	P9_RXATTRCREATE	= 33,
	P9_TREADDIR		= 40,
	P9_RREADDIR		= 41,
	P9_TFSYNC		= 50,
	P9_RFSYNC		= 51,
	P9_TLOCK		= 52,
	P9_RLOCK		= 53,
	P9_TGETLOCK		= 54,
	P9_RGETLOCK		= 55,
	P9_TLINK		= 70,
	P9_RLINK		= 71,
	P9_TMKDIR		= 72,
	P9_RMKDIR		= 73,
	P9_TRENAMEAT	= 74,
	P9_RRENAMEAT	= 75,
	P9_TUNLINKAT	= 76,
	P9_RUNLINKAT	= 77,

	// 9P2000 base messages
	P9_TVERSION		= 100,
	P9_RVERSION		= 101,
	P9_TAUTH		= 102,
	P9_RAUTH		= 103,
	P9_TATTACH		= 104,
	P9_RATTACH		= 105,
	P9_TERROR		= 106,	// not used
	P9_RERROR		= 107,
	P9_TFLUSH		= 108,
	P9_RFLUSH		= 109,
	P9_TWALK		= 110,
	P9_RWALK		= 111,
	P9_TOPEN		= 112,
	P9_ROPEN		= 113,
	P9_TCREATE		= 114,
	P9_RCREATE		= 115,
	P9_TREAD		= 116,
	P9_RREAD		= 117,
	P9_TWRITE		= 118,
	P9_RWRITE		= 119,
	P9_TCLUNK		= 120,
	P9_RCLUNK		= 121,
	P9_TREMOVE		= 122,
	P9_RREMOVE		= 123,
	P9_TSTAT		= 124,
	P9_RSTAT		= 125,
	P9_TWSTAT		= 126,
	P9_RWSTAT		= 127
};

// QID types (file type indicators)
enum P9QidType {
	P9_QTDIR		= 0x80,	// directory
	P9_QTAPPEND		= 0x40,	// append-only file
	P9_QTEXCL		= 0x20,	// exclusive use file
	P9_QTMOUNT		= 0x10,	// mounted channel
	P9_QTAUTH		= 0x08,	// authentication file
	P9_QTTMP		= 0x04,	// temporary file
	P9_QTSYMLINK	= 0x02,	// symbolic link (9P2000.u)
	P9_QTLINK		= 0x01,	// hard link (9P2000.u)
	P9_QTFILE		= 0x00	// regular file
};

// Open/Create flags for 9P2000.L
enum P9OpenFlags {
	P9_OREAD		= 0x00000000,
	P9_OWRITE		= 0x00000001,
	P9_ORDWR		= 0x00000002,
	P9_OACCMODE		= 0x00000003,
	P9_OCREATE		= 0x00000040,
	P9_OEXCL		= 0x00000080,
	P9_ONOCTTY		= 0x00000100,
	P9_OTRUNC		= 0x00000200,
	P9_OAPPEND		= 0x00000400,
	P9_ONONBLOCK	= 0x00000800,
	P9_ODSYNC		= 0x00001000,
	P9_OFASYNC		= 0x00002000,
	P9_ODIRECT		= 0x00004000,
	P9_OLARGEFILE	= 0x00008000,
	P9_ODIRECTORY	= 0x00010000,
	P9_ONOFOLLOW	= 0x00020000,
	P9_ONOATIME		= 0x00040000,
	P9_OCLOEXEC		= 0x00080000,
	P9_OSYNC		= 0x00100000
};

// GETATTR request mask
enum P9GetattrMask {
	P9_GETATTR_MODE		= 0x00000001,
	P9_GETATTR_NLINK	= 0x00000002,
	P9_GETATTR_UID		= 0x00000004,
	P9_GETATTR_GID		= 0x00000008,
	P9_GETATTR_RDEV		= 0x00000010,
	P9_GETATTR_ATIME	= 0x00000020,
	P9_GETATTR_MTIME	= 0x00000040,
	P9_GETATTR_CTIME	= 0x00000080,
	P9_GETATTR_INO		= 0x00000100,
	P9_GETATTR_SIZE		= 0x00000200,
	P9_GETATTR_BLOCKS	= 0x00000400,
	P9_GETATTR_BTIME	= 0x00000800,
	P9_GETATTR_GEN		= 0x00001000,
	P9_GETATTR_DATA_VERSION	= 0x00002000,
	P9_GETATTR_BASIC	= 0x000007ff,
	P9_GETATTR_ALL		= 0x00003fff
};

// SETATTR request mask
enum P9SetattrMask {
	P9_SETATTR_MODE		= 0x00000001,
	P9_SETATTR_UID		= 0x00000002,
	P9_SETATTR_GID		= 0x00000004,
	P9_SETATTR_SIZE		= 0x00000008,
	P9_SETATTR_ATIME	= 0x00000010,
	P9_SETATTR_MTIME	= 0x00000020,
	P9_SETATTR_CTIME	= 0x00000040,
	P9_SETATTR_ATIME_SET	= 0x00000080,
	P9_SETATTR_MTIME_SET	= 0x00000100
};

// Lock types
enum P9LockType {
	P9_LOCK_TYPE_RDLCK	= 0,
	P9_LOCK_TYPE_WRLCK	= 1,
	P9_LOCK_TYPE_UNLCK	= 2
};

// Lock status
enum P9LockStatus {
	P9_LOCK_SUCCESS		= 0,
	P9_LOCK_BLOCKED		= 1,
	P9_LOCK_ERROR		= 2,
	P9_LOCK_GRACE		= 3
};

// Lock flags
enum P9LockFlags {
	P9_LOCK_FLAGS_BLOCK	= 1,
	P9_LOCK_FLAGS_RECLAIM	= 2
};

// Unique file identifier (13 bytes)
struct P9Qid {
	uint8	type;		// file type (P9QidType)
	uint32	version;	// version for cache coherence
	uint64	path;		// unique path identifier
} _PACKED;

// File attributes for 9P2000.L GETATTR response
struct P9Attr {
	uint64	valid;		// which fields are valid
	P9Qid	qid;		// file qid
	uint32	mode;		// protection and file type
	uint32	uid;		// user id
	uint32	gid;		// group id
	uint64	nlink;		// number of hard links
	uint64	rdev;		// device number (for special files)
	uint64	size;		// file size in bytes
	uint64	blksize;	// block size for I/O
	uint64	blocks;		// number of 512-byte blocks
	uint64	atime_sec;	// access time (seconds)
	uint64	atime_nsec;	// access time (nanoseconds)
	uint64	mtime_sec;	// modification time (seconds)
	uint64	mtime_nsec;	// modification time (nanoseconds)
	uint64	ctime_sec;	// change time (seconds)
	uint64	ctime_nsec;	// change time (nanoseconds)
	uint64	btime_sec;	// birth time (seconds)
	uint64	btime_nsec;	// birth time (nanoseconds)
	uint64	gen;		// generation number
	uint64	data_version;	// data version
};

// Directory entry from READDIR
struct P9DirEnt {
	P9Qid	qid;
	uint64	offset;		// offset for next readdir
	uint8	type;		// file type
	char*	name;		// file name (null-terminated)
};

// Filesystem statistics
struct P9StatFS {
	uint32	type;		// filesystem type
	uint32	bsize;		// block size
	uint64	blocks;		// total blocks
	uint64	bfree;		// free blocks
	uint64	bavail;		// available blocks (non-superuser)
	uint64	files;		// total file nodes
	uint64	ffree;		// free file nodes
	uint64	fsid;		// filesystem id
	uint32	namelen;	// maximum filename length
};

// 9P message header (7 bytes)
struct P9Header {
	uint32	size;		// total message size including header
	uint8	type;		// message type
	uint16	tag;		// transaction tag
} _PACKED;

#define P9_HEADER_SIZE	7
#define P9_QID_SIZE		13

// Error codes (Linux errno values used in 9P2000.L)
#define P9_EPERM		1
#define P9_ENOENT		2
#define P9_EIO			5
#define P9_ENXIO		6
#define P9_EACCES		13
#define P9_EEXIST		17
#define P9_EXDEV		18
#define P9_ENODEV		19
#define P9_ENOTDIR		20
#define P9_EISDIR		21
#define P9_EINVAL		22
#define P9_ENFILE		23
#define P9_EMFILE		24
#define P9_ENOSPC		28
#define P9_ESPIPE		29
#define P9_EROFS		30
#define P9_ENAMETOOLONG	36
#define P9_ENOTEMPTY	39
#define P9_ENODATA		61
#define P9_EOVERFLOW	75
#define P9_EOPNOTSUPP	95

// Convert Linux errno to Haiku status_t
status_t p9_error_to_haiku(uint32 error);

// Convert Haiku open flags to 9P2000.L flags
uint32 haiku_to_p9_open_flags(int flags);

// Convert 9P mode to Haiku mode
mode_t p9_mode_to_haiku(uint32 mode);

// Convert Haiku mode to 9P mode
uint32 haiku_mode_to_p9(mode_t mode);

#endif // _9P_H
