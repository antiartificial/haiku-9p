# 9P Filesystem for Haiku OS

A 9P2000.L filesystem implementation for Haiku OS, enabling file sharing between a QEMU host and Haiku guest via virtio-9p.

## What is 9P?

9P is a network filesystem protocol from Plan 9. The 9P2000.L variant adds Linux compatibility. QEMU uses it for host-guest file sharing via virtio-9p.

## Quick Start

### On the Host (QEMU)

```bash
qemu-system-x86_64 \
    -enable-kvm -m 2G \
    -hda haiku.qcow2 \
    -virtfs local,path=/home/user/shared,mount_tag=host,security_model=mapped-xattr
```

### In Haiku (Guest)

```bash
# Clone and build
git clone https://github.com/user/haiku-9p ~/9p
git clone https://review.haiku-os.org/haiku ~/haiku  # First time only

cd ~/9p
./build.sh setup
./build.sh install

# Mount shared folder
mkdir /mnt/host
mount -t 9p -o tag=host /mnt/host
ls /mnt/host
```

## Documentation

- [INSTALL.md](INSTALL.md) - Full installation and usage guide
- [docs/9P_PROTOCOL.md](docs/9P_PROTOCOL.md) - 9P2000.L protocol reference
- [tests/README.md](tests/README.md) - Testing guide

## Features

- 9P2000.L protocol (Linux-compatible)
- Virtio-9p transport
- File read/write
- Directory listing
- File/directory creation and deletion
- Symbolic links
- File attributes (stat)
- Filesystem statistics

## Project Structure

```
src/add-ons/kernel/file_systems/9p/
├── 9p.h              # Protocol definitions
├── 9p_message.cpp    # Message encoding/decoding
├── 9p_client.cpp     # 9P protocol client
├── virtio_9p.cpp     # Virtio transport
├── Volume.cpp        # VFS volume operations
├── Inode.cpp         # VFS inode operations
└── kernel_interface.cpp  # Module registration
```

## Testing

```bash
cd tests

# Protocol unit tests (no server needed)
g++ -std=c++17 test_protocol.cpp -o test_protocol
./test_protocol

# Integration tests
g++ -std=c++17 test_9p_client.cpp -o test_9p_client
bash run_test.sh
```

## License

MIT License - see source files for details.
