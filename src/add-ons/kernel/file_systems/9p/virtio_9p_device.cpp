/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Virtio 9P Device Registry Implementation
 */

#include "virtio_9p_device.h"
#include "virtio_9p.h"

#include <string.h>
#include <lock.h>


struct TransportEntry {
	P9Transport*	transport;
	char			mountTag[256];
	bool			inUse;
};

static TransportEntry sTransports[MAX_VIRTIO_9P_DEVICES];
static mutex sRegistryLock = MUTEX_INITIALIZER("virtio_9p registry");
static bool sInitialized = false;


static void
_InitRegistry()
{
	if (sInitialized)
		return;

	for (int i = 0; i < MAX_VIRTIO_9P_DEVICES; i++) {
		sTransports[i].transport = NULL;
		sTransports[i].mountTag[0] = '\0';
		sTransports[i].inUse = false;
	}
	sInitialized = true;
}


status_t
virtio_9p_register_transport(P9Transport* transport, const char* mountTag)
{
	if (transport == NULL || mountTag == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(&sRegistryLock);
	_InitRegistry();

	// Find free slot
	for (int i = 0; i < MAX_VIRTIO_9P_DEVICES; i++) {
		if (!sTransports[i].inUse) {
			sTransports[i].transport = transport;
			strlcpy(sTransports[i].mountTag, mountTag,
				sizeof(sTransports[i].mountTag));
			sTransports[i].inUse = true;

			dprintf("virtio_9p: registered transport for tag '%s'\n", mountTag);
			return B_OK;
		}
	}

	return B_NO_MEMORY;
}


void
virtio_9p_unregister_transport(P9Transport* transport)
{
	if (transport == NULL)
		return;

	MutexLocker locker(&sRegistryLock);

	for (int i = 0; i < MAX_VIRTIO_9P_DEVICES; i++) {
		if (sTransports[i].inUse && sTransports[i].transport == transport) {
			dprintf("virtio_9p: unregistered transport for tag '%s'\n",
				sTransports[i].mountTag);
			sTransports[i].transport = NULL;
			sTransports[i].mountTag[0] = '\0';
			sTransports[i].inUse = false;
			return;
		}
	}
}


P9Transport*
virtio_9p_find_transport(const char* mountTag)
{
	if (mountTag == NULL)
		return NULL;

	MutexLocker locker(&sRegistryLock);
	_InitRegistry();

	for (int i = 0; i < MAX_VIRTIO_9P_DEVICES; i++) {
		if (sTransports[i].inUse &&
			strcmp(sTransports[i].mountTag, mountTag) == 0) {
			return sTransports[i].transport;
		}
	}

	return NULL;
}
