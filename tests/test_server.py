#!/usr/bin/env python3
"""
Simple 9P2000.L server for testing the Haiku 9P client.

Usage:
    mkdir -p /tmp/9ptest && echo 'Hello World' > /tmp/9ptest/test.txt
    python3 test_server.py [port] [root_path]

Default: port=5640, root=/tmp/9ptest
"""

import os
import sys
import struct
import socket
import stat as st

# Message types
P9_TVERSION = 100; P9_RVERSION = 101
P9_TAUTH = 102; P9_RAUTH = 103
P9_TATTACH = 104; P9_RATTACH = 105
P9_RLERROR = 7
P9_TSTATFS = 8; P9_RSTATFS = 9
P9_TLOPEN = 12; P9_RLOPEN = 13
P9_TLCREATE = 14; P9_RLCREATE = 15
P9_TREADLINK = 22; P9_RREADLINK = 23
P9_TGETATTR = 24; P9_RGETATTR = 25
P9_TSETATTR = 26; P9_RSETATTR = 27
P9_TREADDIR = 40; P9_RREADDIR = 41
P9_TFSYNC = 50; P9_RFSYNC = 51
P9_TMKDIR = 72; P9_RMKDIR = 73
P9_TRENAMEAT = 74; P9_RRENAMEAT = 75
P9_TUNLINKAT = 76; P9_RUNLINKAT = 77
P9_TWALK = 110; P9_RWALK = 111
P9_TREAD = 116; P9_RREAD = 117
P9_TWRITE = 118; P9_RWRITE = 119
P9_TCLUNK = 120; P9_RCLUNK = 121
P9_TREMOVE = 122; P9_RREMOVE = 123

# QID types
QTDIR = 0x80
QTFILE = 0x00
QTSYMLINK = 0x02

# Errors
ENOENT = 2
ENOTDIR = 20
EISDIR = 21
EINVAL = 22
ENOTEMPTY = 39

class Qid:
    def __init__(self, path, stat_result=None):
        self.path = path
        if stat_result:
            self.type = QTDIR if st.S_ISDIR(stat_result.st_mode) else \
                        QTSYMLINK if st.S_ISLNK(stat_result.st_mode) else QTFILE
            self.version = int(stat_result.st_mtime) & 0xFFFFFFFF
        else:
            self.type = QTDIR
            self.version = 0
        self.ino = hash(path) & 0xFFFFFFFFFFFFFFFF

    def pack(self):
        return struct.pack('<BIQ', self.type, self.version, self.ino)

class P9Server:
    def __init__(self, root_path):
        self.root = os.path.abspath(root_path)
        self.fids = {}  # fid -> path
        self.msize = 8192

    def handle_client(self, conn):
        print(f"Client connected")
        try:
            while True:
                # Read message header
                header = conn.recv(4)
                if not header:
                    break
                size = struct.unpack('<I', header)[0]
                data = header + conn.recv(size - 4)

                # Parse and handle message
                response = self.handle_message(data)
                if response:
                    conn.send(response)
        except Exception as e:
            print(f"Error: {e}")
        finally:
            print("Client disconnected")

    def handle_message(self, data):
        size, mtype, tag = struct.unpack('<IBH', data[:7])
        payload = data[7:]
        print(f"  <- T{mtype} tag={tag} size={size}")

        handlers = {
            P9_TVERSION: self.handle_version,
            P9_TATTACH: self.handle_attach,
            P9_TWALK: self.handle_walk,
            P9_TGETATTR: self.handle_getattr,
            P9_TSTATFS: self.handle_statfs,
            P9_TLOPEN: self.handle_lopen,
            P9_TREADDIR: self.handle_readdir,
            P9_TREAD: self.handle_read,
            P9_TCLUNK: self.handle_clunk,
        }

        handler = handlers.get(mtype)
        if handler:
            return handler(tag, payload)
        else:
            print(f"  Unhandled message type: {mtype}")
            return self.error(tag, EINVAL)

    def pack_response(self, mtype, tag, payload):
        size = 7 + len(payload)
        return struct.pack('<IBH', size, mtype, tag) + payload

    def error(self, tag, errno):
        print(f"  -> Rlerror errno={errno}")
        return self.pack_response(P9_RLERROR, tag, struct.pack('<I', errno))

    def read_string(self, data, offset):
        slen = struct.unpack_from('<H', data, offset)[0]
        s = data[offset+2:offset+2+slen].decode('utf-8')
        return s, offset + 2 + slen

    def pack_string(self, s):
        encoded = s.encode('utf-8')
        return struct.pack('<H', len(encoded)) + encoded

    def handle_version(self, tag, payload):
        msize, = struct.unpack_from('<I', payload, 0)
        version, _ = self.read_string(payload, 4)
        print(f"  Tversion msize={msize} version={version}")

        # Negotiate
        self.msize = min(msize, 8192)
        resp_version = "9P2000.L" if "9P2000.L" in version else "unknown"

        resp = struct.pack('<I', self.msize) + self.pack_string(resp_version)
        print(f"  -> Rversion msize={self.msize} version={resp_version}")
        return self.pack_response(P9_RVERSION, tag, resp)

    def handle_attach(self, tag, payload):
        fid, afid = struct.unpack_from('<II', payload, 0)
        uname, offset = self.read_string(payload, 8)
        aname, offset = self.read_string(payload, offset)
        print(f"  Tattach fid={fid} afid={afid} uname={uname} aname={aname}")

        # Attach to root
        self.fids[fid] = self.root
        qid = Qid(self.root, os.stat(self.root))

        print(f"  -> Rattach qid.type=0x{qid.type:02x}")
        return self.pack_response(P9_RATTACH, tag, qid.pack())

    def handle_walk(self, tag, payload):
        fid, newfid, nwname = struct.unpack_from('<IIH', payload, 0)
        offset = 10
        names = []
        for _ in range(nwname):
            name, offset = self.read_string(payload, offset)
            names.append(name)
        print(f"  Twalk fid={fid} newfid={newfid} nwname={nwname} names={names}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        qids = []

        for name in names:
            path = os.path.join(path, name)
            if not os.path.exists(path):
                break
            qids.append(Qid(path, os.stat(path)))

        if len(qids) == nwname or nwname == 0:
            self.fids[newfid] = path

        resp = struct.pack('<H', len(qids))
        for qid in qids:
            resp += qid.pack()

        print(f"  -> Rwalk nwqid={len(qids)}")
        return self.pack_response(P9_RWALK, tag, resp)

    def handle_getattr(self, tag, payload):
        fid, mask = struct.unpack_from('<IQ', payload, 0)
        print(f"  Tgetattr fid={fid} mask=0x{mask:x}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        s = os.stat(path)
        qid = Qid(path, s)

        resp = struct.pack('<Q', mask)  # valid
        resp += qid.pack()
        resp += struct.pack('<IIIQQQQQ',
            s.st_mode, s.st_uid, s.st_gid,
            s.st_nlink, s.st_rdev if hasattr(s, 'st_rdev') else 0,
            s.st_size, s.st_blksize if hasattr(s, 'st_blksize') else 4096,
            s.st_blocks if hasattr(s, 'st_blocks') else 0)
        # Times
        resp += struct.pack('<QQQQQQQQQQ',
            int(s.st_atime), 0,  # atime_sec, atime_nsec
            int(s.st_mtime), 0,  # mtime_sec, mtime_nsec
            int(s.st_ctime), 0,  # ctime_sec, ctime_nsec
            0, 0,  # btime_sec, btime_nsec
            0, 0)  # gen, data_version

        print(f"  -> Rgetattr mode=0o{s.st_mode:o} size={s.st_size}")
        return self.pack_response(P9_RGETATTR, tag, resp)

    def handle_statfs(self, tag, payload):
        fid, = struct.unpack_from('<I', payload, 0)
        print(f"  Tstatfs fid={fid}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        try:
            s = os.statvfs(path)
            resp = struct.pack('<IIQQQQQQI',
                0,  # type
                s.f_bsize,  # bsize
                s.f_blocks,  # blocks
                s.f_bfree,  # bfree
                s.f_bavail,  # bavail
                s.f_files,  # files
                s.f_ffree,  # ffree
                0,  # fsid
                s.f_namemax)  # namelen
        except:
            resp = struct.pack('<IIQQQQQQI', 0, 4096, 1000000, 500000, 500000, 100000, 50000, 0, 255)

        print(f"  -> Rstatfs")
        return self.pack_response(P9_RSTATFS, tag, resp)

    def handle_lopen(self, tag, payload):
        fid, flags = struct.unpack_from('<II', payload, 0)
        print(f"  Tlopen fid={fid} flags=0x{flags:x}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        s = os.stat(path)
        qid = Qid(path, s)

        iounit = self.msize - 24
        resp = qid.pack() + struct.pack('<I', iounit)

        print(f"  -> Rlopen iounit={iounit}")
        return self.pack_response(P9_RLOPEN, tag, resp)

    def handle_readdir(self, tag, payload):
        fid, offset, count = struct.unpack_from('<IQI', payload, 0)
        print(f"  Treaddir fid={fid} offset={offset} count={count}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        if not os.path.isdir(path):
            return self.error(tag, ENOTDIR)

        entries = []
        try:
            for i, name in enumerate(os.listdir(path)):
                if i < offset:
                    continue
                entry_path = os.path.join(path, name)
                s = os.stat(entry_path)
                qid = Qid(entry_path, s)

                # Pack entry: qid[13] offset[8] type[1] name[s]
                entry = qid.pack()
                entry += struct.pack('<QB', i + 1, qid.type)
                entry += self.pack_string(name)
                entries.append(entry)

                if sum(len(e) for e in entries) > count - 4:
                    entries.pop()
                    break
        except Exception as e:
            print(f"  readdir error: {e}")

        data = b''.join(entries)
        resp = struct.pack('<I', len(data)) + data

        print(f"  -> Rreaddir count={len(data)} entries={len(entries)}")
        return self.pack_response(P9_RREADDIR, tag, resp)

    def handle_read(self, tag, payload):
        fid, offset, count = struct.unpack_from('<IQI', payload, 0)
        print(f"  Tread fid={fid} offset={offset} count={count}")

        if fid not in self.fids:
            return self.error(tag, ENOENT)

        path = self.fids[fid]
        try:
            with open(path, 'rb') as f:
                f.seek(offset)
                data = f.read(count)
        except Exception as e:
            print(f"  read error: {e}")
            return self.error(tag, EINVAL)

        resp = struct.pack('<I', len(data)) + data
        print(f"  -> Rread count={len(data)}")
        return self.pack_response(P9_RREAD, tag, resp)

    def handle_clunk(self, tag, payload):
        fid, = struct.unpack_from('<I', payload, 0)
        print(f"  Tclunk fid={fid}")

        if fid in self.fids:
            del self.fids[fid]

        print(f"  -> Rclunk")
        return self.pack_response(P9_RCLUNK, tag, b'')

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5640
    root = sys.argv[2] if len(sys.argv) > 2 else '/tmp/9ptest'

    # Create test directory if needed
    os.makedirs(root, exist_ok=True)
    test_file = os.path.join(root, 'test.txt')
    if not os.path.exists(test_file):
        with open(test_file, 'w') as f:
            f.write('Hello World\n')

    server = P9Server(root)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', port))
    sock.listen(1)

    print(f"9P2000.L test server listening on port {port}")
    print(f"Serving directory: {root}")
    print(f"Press Ctrl+C to stop\n")

    try:
        while True:
            conn, addr = sock.accept()
            server.handle_client(conn)
            conn.close()
    except KeyboardInterrupt:
        print("\nShutting down")
    finally:
        sock.close()

if __name__ == '__main__':
    main()
