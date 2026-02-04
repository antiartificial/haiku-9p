/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Virtio 9P Device Registry
 *
 * This provides a simple registry for virtio-9p transports that the
 * filesystem can query to find a transport matching a given mount tag.
 */
#ifndef _VIRTIO_9P_DEVICE_H
#define _VIRTIO_9P_DEVICE_H

#include "transport.h"

// Maximum number of virtio-9p devices
#define MAX_VIRTIO_9P_DEVICES 8

// Register a virtio-9p transport (called by virtio driver)
status_t virtio_9p_register_transport(P9Transport* transport, const char* mountTag);

// Unregister a virtio-9p transport
void virtio_9p_unregister_transport(P9Transport* transport);

// Find a transport by mount tag (called by filesystem)
P9Transport* virtio_9p_find_transport(const char* mountTag);

#endif // _VIRTIO_9P_DEVICE_H
