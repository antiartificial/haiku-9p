/*
 * Standalone test for 9P protocol message encoding/decoding
 *
 * Build: g++ -std=c++17 -I../src/add-ons/kernel/file_systems/9p test_protocol.cpp -o test_protocol
 *
 * This tests the protocol logic without needing Haiku kernel headers.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>

// Mock Haiku types for testing
typedef int32_t status_t;
typedef uint64_t ino_t;
typedef uint32_t mode_t;
#define B_OK 0
#define B_ERROR -1
#define B_NO_MEMORY -2
#define B_BUFFER_OVERFLOW -3
#define B_NAME_TOO_LONG -4
#define B_BAD_VALUE -5

// Include protocol definitions (we'll inline what we need)
#define P9_VERSION_9P2000_L	"9P2000.L"
#define P9_DEFAULT_MSIZE	8192
#define P9_NOTAG			((uint16_t)~0)
#define P9_NOFID			((uint32_t)~0)
#define P9_HEADER_SIZE		7

enum P9MessageType {
	P9_TVERSION		= 100,
	P9_RVERSION		= 101,
	P9_TATTACH		= 104,
	P9_RATTACH		= 105,
	P9_TWALK		= 110,
	P9_RWALK		= 111,
	P9_TREAD		= 116,
	P9_RREAD		= 117,
};

struct P9Qid {
	uint8_t		type;
	uint32_t	version;
	uint64_t	path;
} __attribute__((packed));

// Simple buffer class for testing
class TestBuffer {
public:
	uint8_t* data;
	size_t capacity;
	size_t writePos;
	size_t readPos;

	TestBuffer(size_t cap) : capacity(cap), writePos(0), readPos(0) {
		data = new uint8_t[cap];
	}
	~TestBuffer() { delete[] data; }

	void reset() { writePos = readPos = 0; }

	// Little-endian write
	void writeU8(uint8_t v) { data[writePos++] = v; }
	void writeU16(uint16_t v) {
		data[writePos++] = v & 0xff;
		data[writePos++] = (v >> 8) & 0xff;
	}
	void writeU32(uint32_t v) {
		data[writePos++] = v & 0xff;
		data[writePos++] = (v >> 8) & 0xff;
		data[writePos++] = (v >> 16) & 0xff;
		data[writePos++] = (v >> 24) & 0xff;
	}
	void writeU64(uint64_t v) {
		writeU32(v & 0xffffffff);
		writeU32(v >> 32);
	}
	void writeString(const char* s) {
		uint16_t len = strlen(s);
		writeU16(len);
		memcpy(data + writePos, s, len);
		writePos += len;
	}

	// Little-endian read
	uint8_t readU8() { return data[readPos++]; }
	uint16_t readU16() {
		uint16_t v = data[readPos] | (data[readPos+1] << 8);
		readPos += 2;
		return v;
	}
	uint32_t readU32() {
		uint32_t v = data[readPos] | (data[readPos+1] << 8) |
					 (data[readPos+2] << 16) | (data[readPos+3] << 24);
		readPos += 4;
		return v;
	}
	uint64_t readU64() {
		uint64_t lo = readU32();
		uint64_t hi = readU32();
		return lo | (hi << 32);
	}
	void readString(char* buf, size_t bufSize) {
		uint16_t len = readU16();
		if (len < bufSize) {
			memcpy(buf, data + readPos, len);
			buf[len] = '\0';
		}
		readPos += len;
	}
	P9Qid readQid() {
		P9Qid qid;
		qid.type = readU8();
		qid.version = readU32();
		qid.path = readU64();
		return qid;
	}

	void finalize() {
		// Write size at beginning
		uint32_t size = writePos;
		data[0] = size & 0xff;
		data[1] = (size >> 8) & 0xff;
		data[2] = (size >> 16) & 0xff;
		data[3] = (size >> 24) & 0xff;
	}
};

// Build Tversion message
void buildTversion(TestBuffer& buf, uint32_t msize, const char* version) {
	buf.reset();
	buf.writeU32(0);  // size placeholder
	buf.writeU8(P9_TVERSION);
	buf.writeU16(P9_NOTAG);
	buf.writeU32(msize);
	buf.writeString(version);
	buf.finalize();
}

// Build Twalk message
void buildTwalk(TestBuffer& buf, uint16_t tag, uint32_t fid, uint32_t newfid,
				int nwname, const char** wnames) {
	buf.reset();
	buf.writeU32(0);
	buf.writeU8(P9_TWALK);
	buf.writeU16(tag);
	buf.writeU32(fid);
	buf.writeU32(newfid);
	buf.writeU16(nwname);
	for (int i = 0; i < nwname; i++) {
		buf.writeString(wnames[i]);
	}
	buf.finalize();
}

// Parse Rversion response
bool parseRversion(TestBuffer& buf, uint32_t* msize, char* version, size_t versionSize) {
	buf.readPos = 0;
	uint32_t size = buf.readU32();
	uint8_t type = buf.readU8();
	uint16_t tag = buf.readU16();

	if (type != P9_RVERSION) {
		printf("Expected Rversion (101), got %d\n", type);
		return false;
	}

	*msize = buf.readU32();
	buf.readString(version, versionSize);
	return true;
}

// Test message encoding
void test_version_encoding() {
	printf("Testing Tversion encoding... ");

	TestBuffer buf(256);
	buildTversion(buf, P9_DEFAULT_MSIZE, P9_VERSION_9P2000_L);

	// Verify header
	buf.readPos = 0;
	uint32_t size = buf.readU32();
	uint8_t type = buf.readU8();
	uint16_t tag = buf.readU16();

	assert(type == P9_TVERSION);
	assert(tag == P9_NOTAG);
	assert(size == buf.writePos);

	uint32_t msize = buf.readU32();
	assert(msize == P9_DEFAULT_MSIZE);

	char version[32];
	buf.readString(version, sizeof(version));
	assert(strcmp(version, P9_VERSION_9P2000_L) == 0);

	printf("OK\n");
}

void test_walk_encoding() {
	printf("Testing Twalk encoding... ");

	TestBuffer buf(256);
	const char* path[] = {"usr", "local", "bin"};
	buildTwalk(buf, 1, 0, 1, 3, path);

	buf.readPos = 0;
	uint32_t size = buf.readU32();
	uint8_t type = buf.readU8();
	uint16_t tag = buf.readU16();

	assert(type == P9_TWALK);
	assert(tag == 1);

	uint32_t fid = buf.readU32();
	uint32_t newfid = buf.readU32();
	uint16_t nwname = buf.readU16();

	assert(fid == 0);
	assert(newfid == 1);
	assert(nwname == 3);

	char name[64];
	buf.readString(name, sizeof(name));
	assert(strcmp(name, "usr") == 0);
	buf.readString(name, sizeof(name));
	assert(strcmp(name, "local") == 0);
	buf.readString(name, sizeof(name));
	assert(strcmp(name, "bin") == 0);

	printf("OK\n");
}

void test_qid_encoding() {
	printf("Testing QID encoding... ");

	TestBuffer buf(64);
	buf.writeU8(0x80);  // QTDIR
	buf.writeU32(12345);
	buf.writeU64(0xDEADBEEFCAFEBABEULL);

	buf.readPos = 0;
	P9Qid qid = buf.readQid();

	assert(qid.type == 0x80);
	assert(qid.version == 12345);
	assert(qid.path == 0xDEADBEEFCAFEBABEULL);

	printf("OK\n");
}

void test_rversion_parsing() {
	printf("Testing Rversion parsing... ");

	// Simulate server response
	TestBuffer buf(256);
	buf.writeU32(0);  // size placeholder
	buf.writeU8(P9_RVERSION);
	buf.writeU16(P9_NOTAG);
	buf.writeU32(4096);  // server msize
	buf.writeString("9P2000.L");
	buf.finalize();

	uint32_t msize;
	char version[32];
	assert(parseRversion(buf, &msize, version, sizeof(version)));
	assert(msize == 4096);
	assert(strcmp(version, "9P2000.L") == 0);

	printf("OK\n");
}

void test_endianness() {
	printf("Testing little-endian encoding... ");

	TestBuffer buf(64);
	buf.writeU32(0x12345678);

	// Should be little-endian: 78 56 34 12
	assert(buf.data[0] == 0x78);
	assert(buf.data[1] == 0x56);
	assert(buf.data[2] == 0x34);
	assert(buf.data[3] == 0x12);

	buf.readPos = 0;
	assert(buf.readU32() == 0x12345678);

	printf("OK\n");
}

int main() {
	printf("\n=== 9P Protocol Tests ===\n\n");

	test_endianness();
	test_version_encoding();
	test_walk_encoding();
	test_qid_encoding();
	test_rversion_parsing();

	printf("\n=== All tests passed! ===\n\n");
	return 0;
}
