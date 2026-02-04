# 9P Filesystem Tests

This directory contains tests for the 9P filesystem implementation.

## Quick Start

### 1. Run the Protocol Unit Tests

```bash
# Build and run the standalone protocol tests (no dependencies)
g++ -std=c++17 test_protocol.cpp -o test_protocol
./test_protocol
```

### 2. Run Integration Tests with Python Server

```bash
# Terminal 1: Start the test server
python3 test_server.py

# Terminal 2: Build and run the client test
g++ -std=c++17 test_9p_client.cpp -o test_9p_client
./test_9p_client localhost 5640
```

### 3. Run Integration Tests with diod (more complete)

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
| `test_protocol.cpp` | Standalone unit tests for message encoding/decoding |
| `test_9p_client.cpp` | Integration test that connects to a real 9P server |
| `test_server.py` | Simple Python 9P2000.L server for testing |

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

Connected to localhost:5640
Sending Tversion...
  Rversion: msize=8192 version=9P2000.L
Sending Tattach (fid=0, aname=)...
  Rattach: qid(type=0x80, ver=..., path=...)
Sending Tgetattr (fid=0)...
  Rgetattr: mode=040755 uid=1000 gid=1000 size=...
...
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
