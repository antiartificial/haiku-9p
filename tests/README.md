# 9P Filesystem Tests

This directory contains tests for the 9P filesystem implementation.

## Quick Start

### 1. Run the Protocol Unit Tests

```bash
# Build and run the standalone protocol tests (no dependencies)
g++ -std=c++17 test_protocol.cpp -o test_protocol
./test_protocol
```

### 2. Run Integration Tests (Recommended)

```bash
# Build the client and run automated test script
g++ -std=c++17 test_9p_client.cpp -o test_9p_client
bash run_test.sh
```

This starts the Python test server, runs the client tests, and reports results.

### 3. Run Integration Tests Manually

```bash
# Terminal 1: Start the test server
python3 test_server.py

# Terminal 2: Run the client test
./test_9p_client localhost 5640
```

### 4. Run Integration Tests with diod (more complete)

```bash
# Install diod (Debian/Ubuntu)
sudo apt-get install diod

# Create test directory
mkdir -p /tmp/9ptest
echo "Hello World" > /tmp/9ptest/test.txt
mkdir /tmp/9ptest/subdir

# Terminal 1: Start diod server
diod -f -n -e /tmp/9ptest -l 0.0.0.0:5640

# Terminal 2: Run client test
./test_9p_client localhost 5640
```

## Test Files

| File | Description |
|------|-------------|
| `run_test.sh` | Automated test script (starts server, runs client, reports results) |
| `test_protocol.cpp` | Standalone unit tests for message encoding/decoding |
| `test_9p_client.cpp` | Integration test that connects to a real 9P server |
| `test_server.py` | Simple Python 9P2000.L server for testing |

## What Gets Tested

### Protocol Unit Tests
- Little-endian encoding/decoding
- Tversion message encoding
- Twalk message encoding
- QID structure encoding
- Rversion response parsing

### Integration Tests
The integration tests verify these 9P2000.L operations:

| Operation | Description |
|-----------|-------------|
| Tversion/Rversion | Protocol version negotiation |
| Tattach/Rattach | Connect to filesystem root |
| Tgetattr/Rgetattr | Get file/directory attributes |
| Tstatfs/Rstatfs | Get filesystem statistics |
| Twalk/Rwalk | Path traversal and FID cloning |
| Tlopen/Rlopen | Open file or directory |
| Treaddir/Rreaddir | Read directory entries |
| Tclunk/Rclunk | Release FID (close) |

## Expected Output

### Protocol Tests
```
=== 9P Protocol Tests ===

Testing little-endian encoding... OK
Testing Tversion encoding... OK
Testing Twalk encoding... OK
Testing QID encoding... OK
Testing Rversion parsing... OK

=== All tests passed! ===
```

### Client Integration Tests
```
=== 9P Client Integration Test ===

Connected to localhost:5660
Sending Tversion...
  Rversion: msize=8192 version=9P2000.L
Sending Tattach (fid=0, aname=)...
  Rattach: qid(type=0x80, ver=..., path=...)
Sending Tgetattr (fid=0)...
  Rgetattr: mode=040755 uid=1000 gid=1000 size=60
Sending Tstatfs (fid=0)...
  Rstatfs: bsize=4096 blocks=... bfree=...
Sending Twalk (fid=0, newfid=1, nwname=0)...
  Rwalk: nwqid=0
Sending Tlopen (fid=1, flags=0x0)...
  Rlopen: qid(type=0x80) iounit=8168
Sending Treaddir (fid=1)...
  Rreaddir: count=32 bytes
    test.txt (type=0x00, path=...)
Sending Tclunk (fid=1)...
  Rclunk: OK
Sending Twalk (fid=0, newfid=2, nwname=1)...
  Rwalk: nwqid=1
    [0] qid(type=0x00, ver=..., path=...)
Sending Tgetattr (fid=2)...
  Rgetattr: mode=0100644 uid=1000 gid=1000 size=12
Sending Tclunk (fid=2)...
  Rclunk: OK
Sending Tclunk (fid=0)...
  Rclunk: OK

=== Test completed ===
```

## Testing in QEMU with Haiku

Once the module is built and installed in Haiku:

```bash
# Start QEMU with 9P filesystem sharing
qemu-system-x86_64 \
  -enable-kvm \
  -m 2G \
  -hda haiku.qcow2 \
  -virtfs local,path=/home/user/shared,mount_tag=host,security_model=mapped-xattr

# In Haiku terminal:
mkdir /mnt/host
mount -t 9p -o tag=host /mnt/host
ls /mnt/host
cat /mnt/host/test.txt
```

## Debugging

Enable tracing in the kernel module by uncommenting the TRACE defines:

```cpp
// In kernel_interface.cpp, Volume.cpp, Inode.cpp:
#define TRACE_9P_INTERFACE
#define TRACE_9P_VOLUME
#define TRACE_9P_INODE
```

View kernel debug output:
```bash
# In Haiku
syslog | grep 9p
```
