# 9P Filesystem Installation Guide

## Quick Start (Inside Haiku)

```bash
# 1. Clone Haiku source (one-time, takes a while)
git clone https://review.haiku-os.org/haiku ~/haiku

# 2. Clone this repo
git clone https://github.com/your/haiku-9p ~/9p

# 3. Build and install
cd ~/9p
./build.sh setup    # First time only
./build.sh install

# 4. Mount (assuming QEMU was started with mount_tag=host)
mkdir /mnt/host
mount -t 9p -o tag=host /mnt/host
ls /mnt/host
```

## QEMU Setup (Host Side)

Add to your QEMU command:

```bash
qemu-system-x86_64 \
    -enable-kvm \
    -m 2G \
    -hda haiku.qcow2 \
    -virtfs local,path=/home/user/shared,mount_tag=host,security_model=mapped-xattr
```

| Option | Description |
|--------|-------------|
| `path=` | Host directory to share |
| `mount_tag=` | Name used when mounting (e.g., "host") |
| `security_model=` | `mapped-xattr` (recommended), `passthrough`, or `none` |

## Build Script Commands

```bash
./build.sh setup    # Set up Haiku source integration (first time)
./build.sh          # Build the module
./build.sh install  # Build and install (Haiku only)
./build.sh clean    # Clean build artifacts
./build.sh help     # Show help
```

## Manual Build Steps

If you prefer not to use the build script:

### 1. Copy to Haiku Source Tree

```bash
cp -r src/add-ons/kernel/file_systems/9p \
      ~/haiku/src/add-ons/kernel/file_systems/
```

### 2. Add to Build System

Edit `~/haiku/src/add-ons/kernel/file_systems/Jamfile`:

```jam
SubInclude HAIKU_TOP src add-ons kernel file_systems 9p ;
```

### 3. Build

```bash
cd ~/haiku
./configure --use-gcc-pipe  # First time only
jam -q 9p
```

### 4. Install

```bash
cp generated/objects/haiku/*/release/add-ons/kernel/file_systems/9p \
   /boot/system/non-packaged/add-ons/kernel/file_systems/
```

## Usage

### Mount

```bash
# Basic mount
mount -t 9p -o tag=host /mnt/host

# With options
mount -t 9p -o tag=host,aname=subdir /mnt/host
```

### Mount Options

| Option | Description |
|--------|-------------|
| `tag=<name>` | Mount tag (required, must match QEMU) |
| `aname=<path>` | Subdirectory to mount |
| `msize=<bytes>` | Max message size (default: 8192) |

### Unmount

```bash
unmount /mnt/host
```

## Troubleshooting

### "no virtio-9p device found"

- Verify QEMU has `-virtfs` option with correct `mount_tag`
- Check tag matches: QEMU's `mount_tag=X` must match `-o tag=X`
- View registered tags: `syslog | grep virtio_9p`

### Enable Debug Logging

Edit source files and uncomment:

```cpp
#define TRACE_9P_INTERFACE  // kernel_interface.cpp
#define TRACE_9P_VOLUME     // Volume.cpp
#define TRACE_9P_INODE      // Inode.cpp
#define TRACE_VIRTIO_9P     // virtio_9p.cpp
```

Rebuild and check: `syslog | grep 9p`

### Permission Issues

Try different QEMU security models:
- `security_model=mapped-xattr` - Best compatibility
- `security_model=none` - No permission mapping
- `security_model=passthrough` - Requires root

## Testing (Without Haiku)

Run the protocol tests on any Linux system:

```bash
cd tests
g++ -std=c++17 test_protocol.cpp -o test_protocol && ./test_protocol
g++ -std=c++17 test_9p_client.cpp -o test_9p_client && bash run_test.sh
```

## Files

```
haiku-9p/
├── build.sh                    # Build/install script
├── INSTALL.md                  # This file
├── docs/9P_PROTOCOL.md         # Protocol reference
├── src/add-ons/kernel/file_systems/9p/
│   ├── Jamfile                 # Build config
│   ├── 9p.h                    # Protocol definitions
│   ├── 9p_message.cpp          # Message encoding
│   ├── 9p_client.cpp           # Protocol client
│   ├── transport.h             # Transport interface
│   ├── virtio_9p.cpp           # Virtio driver
│   ├── virtio_9p_device.cpp    # Device registry
│   ├── Volume.cpp              # VFS volume ops
│   ├── Inode.cpp               # VFS inode ops
│   └── kernel_interface.cpp    # Module entry
└── tests/
    ├── run_test.sh             # Test runner
    ├── test_protocol.cpp       # Protocol tests
    ├── test_9p_client.cpp      # Integration tests
    └── test_server.py          # Python test server
```
