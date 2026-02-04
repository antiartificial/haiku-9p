/*
 * Integration test that connects to a real 9P server over TCP
 *
 * Build: g++ -std=c++17 test_9p_client.cpp -o test_9p_client
 *
 * Run a 9P server first:
 *   Option 1: diod -f -n -e /tmp/9ptest
 *   Option 2: python3 -m py9p -p 5640 /tmp/9ptest
 *   Option 3: See test_server.py in this directory
 *
 * Then run: ./test_9p_client localhost 5640
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdint.h>

#define P9_VERSION_9P2000_L  "9P2000.L"
#define P9_DEFAULT_MSIZE     8192
#define P9_NOTAG             0xFFFF
#define P9_NOFID             0xFFFFFFFF
#define P9_NONUNAME          0xFFFFFFFF

// Message types
#define P9_TVERSION    100
#define P9_RVERSION    101
#define P9_TAUTH       102
#define P9_RAUTH       103
#define P9_TATTACH     104
#define P9_RATTACH     105
#define P9_RLERROR     7
#define P9_TWALK       110
#define P9_RWALK       111
#define P9_TLOPEN      12
#define P9_RLOPEN      13
#define P9_TREAD       116
#define P9_RREAD       117
#define P9_TCLUNK      120
#define P9_RCLUNK      121
#define P9_TGETATTR    24
#define P9_RGETATTR    25
#define P9_TREADDIR    40
#define P9_RREADDIR    41
#define P9_TSTATFS     8
#define P9_RSTATFS     9

// QID types
#define P9_QTDIR       0x80
#define P9_QTFILE      0x00

// GETATTR mask
#define P9_GETATTR_BASIC  0x000007ff

// Open flags
#define P9_OREAD       0

struct P9Qid {
    uint8_t type;
    uint32_t version;
    uint64_t path;
};

class P9Client {
public:
    int sock;
    uint8_t* buf;
    uint32_t msize;
    uint32_t pos;
    uint16_t nextTag;

    P9Client() : sock(-1), buf(nullptr), msize(P9_DEFAULT_MSIZE), pos(0), nextTag(1) {
        buf = new uint8_t[msize];
    }

    ~P9Client() {
        if (sock >= 0) close(sock);
        delete[] buf;
    }

    bool connect(const char* host, int port) {
        struct hostent* he = gethostbyname(host);
        if (!he) {
            printf("Failed to resolve %s\n", host);
            return false;
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            return false;
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);

        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect");
            close(sock);
            sock = -1;
            return false;
        }

        printf("Connected to %s:%d\n", host, port);
        return true;
    }

    // Buffer operations (little-endian)
    void reset() { pos = 0; }

    void writeU8(uint8_t v) { buf[pos++] = v; }
    void writeU16(uint16_t v) {
        buf[pos++] = v & 0xff;
        buf[pos++] = (v >> 8) & 0xff;
    }
    void writeU32(uint32_t v) {
        buf[pos++] = v & 0xff;
        buf[pos++] = (v >> 8) & 0xff;
        buf[pos++] = (v >> 16) & 0xff;
        buf[pos++] = (v >> 24) & 0xff;
    }
    void writeU64(uint64_t v) {
        writeU32(v & 0xffffffff);
        writeU32(v >> 32);
    }
    void writeStr(const char* s) {
        uint16_t len = s ? strlen(s) : 0;
        writeU16(len);
        if (len > 0) {
            memcpy(buf + pos, s, len);
            pos += len;
        }
    }

    uint8_t readU8() { return buf[pos++]; }
    uint16_t readU16() {
        uint16_t v = buf[pos] | (buf[pos+1] << 8);
        pos += 2;
        return v;
    }
    uint32_t readU32() {
        uint32_t v = buf[pos] | (buf[pos+1] << 8) |
                     (buf[pos+2] << 16) | (buf[pos+3] << 24);
        pos += 4;
        return v;
    }
    uint64_t readU64() {
        uint64_t lo = readU32();
        uint64_t hi = readU32();
        return lo | (hi << 32);
    }
    void readStr(char* out, size_t maxLen) {
        uint16_t len = readU16();
        if (len < maxLen) {
            memcpy(out, buf + pos, len);
            out[len] = '\0';
        }
        pos += len;
    }
    P9Qid readQid() {
        P9Qid qid;
        qid.type = readU8();
        qid.version = readU32();
        qid.path = readU64();
        return qid;
    }

    void finalize() {
        uint32_t size = pos;
        buf[0] = size & 0xff;
        buf[1] = (size >> 8) & 0xff;
        buf[2] = (size >> 16) & 0xff;
        buf[3] = (size >> 24) & 0xff;
    }

    bool recvAll(void* buffer, size_t len) {
        uint8_t* p = (uint8_t*)buffer;
        size_t remaining = len;
        while (remaining > 0) {
            ssize_t n = recv(sock, p, remaining, 0);
            if (n <= 0) {
                if (n == 0) printf("  Connection closed by server\n");
                else perror("  recv");
                return false;
            }
            p += n;
            remaining -= n;
        }
        return true;
    }

    bool sendRecv() {
        // Send
        finalize();
        ssize_t sent = send(sock, buf, pos, 0);
        if (sent != (ssize_t)pos) {
            perror("send");
            return false;
        }

        // Receive header first (4 bytes for size)
        reset();
        if (!recvAll(buf, 4)) {
            printf("  Failed to receive header\n");
            return false;
        }
        pos = 4;

        uint32_t size = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
        if (size > msize) {
            printf("Response too large: %u\n", size);
            return false;
        }

        // Receive rest
        if (size > 4 && !recvAll(buf + 4, size - 4)) {
            printf("  Failed to receive body\n");
            return false;
        }

        pos = 0;
        return true;
    }

    // Protocol operations
    bool version() {
        printf("Sending Tversion...\n");
        reset();
        writeU32(0);  // size placeholder
        writeU8(P9_TVERSION);
        writeU16(P9_NOTAG);
        writeU32(msize);
        writeStr(P9_VERSION_9P2000_L);

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RVERSION) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        uint32_t serverMsize = readU32();
        char version[64];
        readStr(version, sizeof(version));

        printf("  Rversion: msize=%u version=%s\n", serverMsize, version);
        if (serverMsize < msize) msize = serverMsize;
        return true;
    }

    bool attach(uint32_t fid, const char* aname) {
        printf("Sending Tattach (fid=%u, aname=%s)...\n", fid, aname);
        reset();
        writeU32(0);
        writeU8(P9_TATTACH);
        writeU16(nextTag++);
        writeU32(fid);       // fid
        writeU32(P9_NOFID);  // afid (no auth)
        writeStr("");        // uname
        writeStr(aname);     // aname
        writeU32(P9_NONUNAME);  // n_uname

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RATTACH) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        P9Qid qid = readQid();
        printf("  Rattach: qid(type=0x%02x, ver=%u, path=%llu)\n",
               qid.type, qid.version, (unsigned long long)qid.path);
        return true;
    }

    bool walk(uint32_t fid, uint32_t newfid, int nwname, const char** names) {
        printf("Sending Twalk (fid=%u, newfid=%u, nwname=%d)...\n",
               fid, newfid, nwname);
        reset();
        writeU32(0);
        writeU8(P9_TWALK);
        writeU16(nextTag++);
        writeU32(fid);
        writeU32(newfid);
        writeU16(nwname);
        for (int i = 0; i < nwname; i++) {
            writeStr(names[i]);
        }

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u (ENOENT=2)\n", err);
            return false;
        }

        if (type != P9_RWALK) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        uint16_t nwqid = readU16();
        printf("  Rwalk: nwqid=%u\n", nwqid);
        for (int i = 0; i < nwqid; i++) {
            P9Qid qid = readQid();
            printf("    [%d] qid(type=0x%02x, ver=%u, path=%llu)\n",
                   i, qid.type, qid.version, (unsigned long long)qid.path);
        }
        return nwqid == nwname;
    }

    bool getattr(uint32_t fid) {
        printf("Sending Tgetattr (fid=%u)...\n", fid);
        reset();
        writeU32(0);
        writeU8(P9_TGETATTR);
        writeU16(nextTag++);
        writeU32(fid);
        writeU64(P9_GETATTR_BASIC);

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RGETATTR) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        uint64_t valid = readU64();
        P9Qid qid = readQid();
        uint32_t mode = readU32();
        uint32_t uid = readU32();
        uint32_t gid = readU32();
        uint64_t nlink = readU64();
        uint64_t rdev = readU64();
        uint64_t fsize = readU64();
        uint64_t blksize = readU64();
        uint64_t blocks = readU64();

        printf("  Rgetattr: mode=0%o uid=%u gid=%u size=%llu\n",
               mode, uid, gid, (unsigned long long)fsize);
        return true;
    }

    bool statfs(uint32_t fid) {
        printf("Sending Tstatfs (fid=%u)...\n", fid);
        reset();
        writeU32(0);
        writeU8(P9_TSTATFS);
        writeU16(nextTag++);
        writeU32(fid);

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RSTATFS) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        uint32_t fstype = readU32();
        uint32_t bsize = readU32();
        uint64_t blocks = readU64();
        uint64_t bfree = readU64();
        uint64_t bavail = readU64();
        uint64_t files = readU64();
        uint64_t ffree = readU64();

        printf("  Rstatfs: bsize=%u blocks=%llu bfree=%llu\n",
               bsize, (unsigned long long)blocks, (unsigned long long)bfree);
        return true;
    }

    bool lopen(uint32_t fid, uint32_t flags) {
        printf("Sending Tlopen (fid=%u, flags=0x%x)...\n", fid, flags);
        reset();
        writeU32(0);
        writeU8(P9_TLOPEN);
        writeU16(nextTag++);
        writeU32(fid);
        writeU32(flags);

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RLOPEN) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        P9Qid qid = readQid();
        uint32_t iounit = readU32();
        printf("  Rlopen: qid(type=0x%02x) iounit=%u\n", qid.type, iounit);
        return true;
    }

    bool readdir(uint32_t fid) {
        printf("Sending Treaddir (fid=%u)...\n", fid);
        reset();
        writeU32(0);
        writeU8(P9_TREADDIR);
        writeU16(nextTag++);
        writeU32(fid);
        writeU64(0);     // offset
        writeU32(4096);  // count

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        if (type != P9_RREADDIR) {
            printf("  Unexpected response type: %d\n", type);
            return false;
        }

        uint32_t count = readU32();
        printf("  Rreaddir: count=%u bytes\n", count);

        // Parse directory entries
        uint32_t end = pos + count;
        while (pos < end) {
            P9Qid qid = readQid();
            uint64_t offset = readU64();
            uint8_t dtype = readU8();
            char name[256];
            readStr(name, sizeof(name));
            printf("    %s (type=0x%02x, path=%llu)\n",
                   name, qid.type, (unsigned long long)qid.path);
        }
        return true;
    }

    bool clunk(uint32_t fid) {
        printf("Sending Tclunk (fid=%u)...\n", fid);
        reset();
        writeU32(0);
        writeU8(P9_TCLUNK);
        writeU16(nextTag++);
        writeU32(fid);

        if (!sendRecv()) return false;

        uint32_t size = readU32();
        uint8_t type = readU8();
        uint16_t tag = readU16();

        if (type == P9_RLERROR) {
            uint32_t err = readU32();
            printf("  Error: %u\n", err);
            return false;
        }

        printf("  Rclunk: OK\n");
        return type == P9_RCLUNK;
    }
};

void usage(const char* prog) {
    printf("Usage: %s <host> <port>\n", prog);
    printf("\nFirst start a 9P server:\n");
    printf("  mkdir -p /tmp/9ptest && echo 'Hello' > /tmp/9ptest/test.txt\n");
    printf("  diod -f -n -e /tmp/9ptest -l 0.0.0.0:5640\n");
    printf("\nOr use the Python test server:\n");
    printf("  python3 test_server.py\n");
}

int main(int argc, char** argv) {
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    const char* host = argv[1];
    int port = atoi(argv[2]);

    printf("\n=== 9P Client Integration Test ===\n\n");

    P9Client client;

    if (!client.connect(host, port)) {
        return 1;
    }

    // Version negotiation
    if (!client.version()) {
        printf("Version negotiation failed\n");
        return 1;
    }

    // Attach to root
    if (!client.attach(0, "")) {
        printf("Attach failed\n");
        return 1;
    }

    // Get root attributes
    client.getattr(0);

    // Get filesystem stats
    client.statfs(0);

    // Clone root fid and open directory
    const char* empty = nullptr;
    if (client.walk(0, 1, 0, &empty)) {
        if (client.lopen(1, P9_OREAD)) {
            client.readdir(1);
        }
        client.clunk(1);
    }

    // Try walking to a file
    const char* path[] = {"test.txt"};
    if (client.walk(0, 2, 1, path)) {
        client.getattr(2);
        client.clunk(2);
    }

    // Cleanup
    client.clunk(0);

    printf("\n=== Test completed ===\n\n");
    return 0;
}
