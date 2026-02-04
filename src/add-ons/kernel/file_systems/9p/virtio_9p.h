/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Virtio 9P Transport
 */
#ifndef _VIRTIO_9P_H
#define _VIRTIO_9P_H

#include "transport.h"

#include <virtio.h>
#include <lock.h>


// Virtio 9P device feature bits
#define VIRTIO_9P_MOUNT_TAG		0x01

// Virtio 9P configuration structure
struct virtio_9p_config {
	uint16	tag_len;
	char	tag[0];
} _PACKED;


class Virtio9PTransport : public P9Transport {
public:
						Virtio9PTransport(device_node* node);
	virtual				~Virtio9PTransport();

	// P9Transport interface
	virtual status_t	Init() override;
	virtual void		Uninit() override;
	virtual status_t	SendMessage(const void* data, size_t size) override;
	virtual status_t	ReceiveMessage(void* buffer, size_t* size) override;
	virtual size_t		MaxMessageSize() const override;
	virtual const char*	Name() const override { return "virtio-9p"; }

	// Get mount tag from device
	const char*			MountTag() const { return fMountTag; }

	// Check if device matches mount tag
	bool				MatchesTag(const char* tag) const;

private:
	static void			_ConfigCallback(void* cookie);
	static void			_QueueCallback(void* driverCookie, void* cookie);

	void				_DumpConfig();

	device_node*		fNode;
	virtio_device_interface* fVirtio;
	virtio_device*		fVirtioDevice;
	virtio_queue		fVirtQueue;

	char*				fMountTag;
	size_t				fMaxSize;

	void*				fRequestBuffer;
	void*				fResponseBuffer;
	physical_entry		fRequestEntry;
	physical_entry		fResponseEntry;

	sem_id				fTransferDone;
	mutex				fLock;

	bool				fInitialized;
};


// Driver module interface
extern device_manager_info* gDeviceManager;

status_t virtio_9p_init_driver(device_node* node, void** cookie);
void virtio_9p_uninit_driver(void* cookie);
status_t virtio_9p_register_device(device_node* parent);
float virtio_9p_supports_device(device_node* parent);

#endif // _VIRTIO_9P_H
