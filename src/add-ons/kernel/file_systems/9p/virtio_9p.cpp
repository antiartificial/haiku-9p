/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Virtio 9P Transport Implementation
 */

#include "virtio_9p.h"

#include <stdlib.h>
#include <string.h>
#include <new>

#include <KernelExport.h>
#include <vm/vm.h>

#include "9p.h"


//#define TRACE_VIRTIO_9P
#ifdef TRACE_VIRTIO_9P
#	define TRACE(x...) dprintf("virtio_9p: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("virtio_9p: " x)


device_manager_info* gDeviceManager = NULL;


Virtio9PTransport::Virtio9PTransport(device_node* node)
	:
	fNode(node),
	fVirtio(NULL),
	fVirtioDevice(NULL),
	fVirtQueue(NULL),
	fMountTag(NULL),
	fMaxSize(P9_MAX_MSIZE),
	fRequestBuffer(NULL),
	fResponseBuffer(NULL),
	fTransferDone(-1),
	fInitialized(false)
{
	mutex_init(&fLock, "virtio_9p transport");
}


Virtio9PTransport::~Virtio9PTransport()
{
	Uninit();
	mutex_destroy(&fLock);
}


status_t
Virtio9PTransport::Init()
{
	TRACE("Init()\n");

	if (fInitialized)
		return B_OK;

	// Get parent virtio device
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == NULL) {
		ERROR("no parent node\n");
		return B_ERROR;
	}

	status_t status = gDeviceManager->get_driver(parent,
		(driver_module_info**)&fVirtio, (void**)&fVirtioDevice);
	gDeviceManager->put_node(parent);
	if (status != B_OK) {
		ERROR("failed to get virtio driver: %s\n", strerror(status));
		return status;
	}

	// Negotiate features - we only need mount tag
	uint64 features = VIRTIO_9P_MOUNT_TAG;
	status = fVirtio->negotiate_features(fVirtioDevice, features, &features, NULL);
	if (status != B_OK) {
		ERROR("failed to negotiate features: %s\n", strerror(status));
		return status;
	}

	// Read mount tag from config
	if (features & VIRTIO_9P_MOUNT_TAG) {
		uint16 tagLen;
		fVirtio->read_device_config(fVirtioDevice,
			offsetof(virtio_9p_config, tag_len), &tagLen, sizeof(tagLen));

		if (tagLen > 0 && tagLen < 256) {
			fMountTag = (char*)malloc(tagLen + 1);
			if (fMountTag != NULL) {
				fVirtio->read_device_config(fVirtioDevice,
					offsetof(virtio_9p_config, tag), fMountTag, tagLen);
				fMountTag[tagLen] = '\0';
				TRACE("mount tag: %s\n", fMountTag);
			}
		}
	}

	// Set up queue
	status = fVirtio->alloc_queues(fVirtioDevice, 1, &fVirtQueue);
	if (status != B_OK) {
		ERROR("failed to allocate virtqueue: %s\n", strerror(status));
		free(fMountTag);
		fMountTag = NULL;
		return status;
	}

	status = fVirtio->setup_interrupt(fVirtioDevice, NULL, this);
	if (status != B_OK) {
		ERROR("failed to set up interrupts: %s\n", strerror(status));
		free(fMountTag);
		fMountTag = NULL;
		return status;
	}

	status = fVirtio->queue_setup_interrupt(fVirtQueue, _QueueCallback, this);
	if (status != B_OK) {
		ERROR("failed to set up queue interrupt: %s\n", strerror(status));
		free(fMountTag);
		fMountTag = NULL;
		return status;
	}

	// Allocate DMA buffers
	fRequestBuffer = malloc(fMaxSize);
	fResponseBuffer = malloc(fMaxSize);
	if (fRequestBuffer == NULL || fResponseBuffer == NULL) {
		ERROR("failed to allocate buffers\n");
		free(fRequestBuffer);
		free(fResponseBuffer);
		free(fMountTag);
		fRequestBuffer = NULL;
		fResponseBuffer = NULL;
		fMountTag = NULL;
		return B_NO_MEMORY;
	}

	// Get physical addresses
	status = get_memory_map(fRequestBuffer, fMaxSize, &fRequestEntry, 1);
	if (status != B_OK || fRequestEntry.size < fMaxSize) {
		ERROR("failed to get request buffer physical address\n");
		free(fRequestBuffer);
		free(fResponseBuffer);
		free(fMountTag);
		fRequestBuffer = NULL;
		fResponseBuffer = NULL;
		fMountTag = NULL;
		return B_ERROR;
	}

	status = get_memory_map(fResponseBuffer, fMaxSize, &fResponseEntry, 1);
	if (status != B_OK || fResponseEntry.size < fMaxSize) {
		ERROR("failed to get response buffer physical address\n");
		free(fRequestBuffer);
		free(fResponseBuffer);
		free(fMountTag);
		fRequestBuffer = NULL;
		fResponseBuffer = NULL;
		fMountTag = NULL;
		return B_ERROR;
	}

	// Create transfer completion semaphore
	fTransferDone = create_sem(0, "virtio_9p transfer");
	if (fTransferDone < 0) {
		ERROR("failed to create semaphore\n");
		free(fRequestBuffer);
		free(fResponseBuffer);
		free(fMountTag);
		fRequestBuffer = NULL;
		fResponseBuffer = NULL;
		fMountTag = NULL;
		return fTransferDone;
	}

	fInitialized = true;
	return B_OK;
}


void
Virtio9PTransport::Uninit()
{
	if (!fInitialized)
		return;

	if (fTransferDone >= 0) {
		delete_sem(fTransferDone);
		fTransferDone = -1;
	}

	free(fRequestBuffer);
	free(fResponseBuffer);
	free(fMountTag);

	fRequestBuffer = NULL;
	fResponseBuffer = NULL;
	fMountTag = NULL;
	fInitialized = false;
}


status_t
Virtio9PTransport::SendMessage(const void* data, size_t size)
{
	TRACE("SendMessage(%p, %zu)\n", data, size);

	if (!fInitialized)
		return B_NO_INIT;

	if (size > fMaxSize)
		return B_BUFFER_OVERFLOW;

	MutexLocker locker(&fLock);

	// Copy request to DMA buffer
	memcpy(fRequestBuffer, data, size);

	return B_OK;
}


status_t
Virtio9PTransport::ReceiveMessage(void* buffer, size_t* size)
{
	TRACE("ReceiveMessage(%p, %zu)\n", buffer, *size);

	if (!fInitialized)
		return B_NO_INIT;

	if (*size > fMaxSize)
		*size = fMaxSize;

	// Get size from request we just copied
	uint32 requestSize;
	memcpy(&requestSize, fRequestBuffer, sizeof(requestSize));
	requestSize = B_LENDIAN_TO_HOST_INT32(requestSize);

	// Set up scatter-gather entries
	physical_entry entries[2];
	entries[0].address = fRequestEntry.address;
	entries[0].size = requestSize;
	entries[1].address = fResponseEntry.address;
	entries[1].size = fMaxSize;

	// Queue request
	status_t status = fVirtio->queue_request_v(fVirtQueue, entries, 1, 1, this);
	if (status != B_OK) {
		ERROR("queue_request_v failed: %s\n", strerror(status));
		return status;
	}

	// Wait for completion
	status = acquire_sem_etc(fTransferDone, 1, B_CAN_INTERRUPT, 0);
	if (status != B_OK) {
		ERROR("acquire_sem failed: %s\n", strerror(status));
		return status;
	}

	// Get response size from header
	uint32 responseSize;
	memcpy(&responseSize, fResponseBuffer, sizeof(responseSize));
	responseSize = B_LENDIAN_TO_HOST_INT32(responseSize);

	if (responseSize > fMaxSize) {
		ERROR("response too large: %u\n", responseSize);
		return B_BUFFER_OVERFLOW;
	}

	// Copy response
	memcpy(buffer, fResponseBuffer, responseSize);
	*size = responseSize;

	TRACE("received %u bytes\n", responseSize);
	return B_OK;
}


size_t
Virtio9PTransport::MaxMessageSize() const
{
	return fMaxSize;
}


bool
Virtio9PTransport::MatchesTag(const char* tag) const
{
	if (tag == NULL || fMountTag == NULL)
		return false;

	return strcmp(tag, fMountTag) == 0;
}


void
Virtio9PTransport::_ConfigCallback(void* cookie)
{
	TRACE("config changed\n");
}


void
Virtio9PTransport::_QueueCallback(void* driverCookie, void* cookie)
{
	Virtio9PTransport* transport = (Virtio9PTransport*)cookie;

	TRACE("queue callback\n");

	// Dequeue completed request
	while (transport->fVirtio->queue_dequeue(transport->fVirtQueue, NULL, NULL))
		;

	release_sem_etc(transport->fTransferDone, 1, B_DO_NOT_RESCHEDULE);
}


void
Virtio9PTransport::_DumpConfig()
{
	if (fMountTag != NULL)
		TRACE("mount tag: %s\n", fMountTag);
}


// #pragma mark - Driver module


status_t
virtio_9p_init_driver(device_node* node, void** cookie)
{
	TRACE("init_driver\n");

	Virtio9PTransport* transport = new(std::nothrow) Virtio9PTransport(node);
	if (transport == NULL)
		return B_NO_MEMORY;

	status_t status = transport->Init();
	if (status != B_OK) {
		delete transport;
		return status;
	}

	*cookie = transport;
	return B_OK;
}


void
virtio_9p_uninit_driver(void* cookie)
{
	TRACE("uninit_driver\n");

	Virtio9PTransport* transport = (Virtio9PTransport*)cookie;
	delete transport;
}


float
virtio_9p_supports_device(device_node* parent)
{
	const char* bus;
	uint16 deviceType;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "virtio") != 0)
		return 0.0f;

	if (gDeviceManager->get_attr_uint16(parent, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK)
		return 0.0f;

	// Virtio device type 9 is 9P transport
	if (deviceType != 9)
		return 0.0f;

	TRACE("found virtio 9p device\n");
	return 0.6f;
}


status_t
virtio_9p_register_device(device_node* parent)
{
	TRACE("register_device\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "Virtio 9P Transport" }},
		{ NULL }
	};

	return gDeviceManager->register_node(parent, "file_systems/9p/virtio_9p/driver_v1",
		attrs, NULL, NULL);
}
