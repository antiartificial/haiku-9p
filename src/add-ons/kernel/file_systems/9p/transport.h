/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * 9P Transport Interface
 */
#ifndef _9P_TRANSPORT_H
#define _9P_TRANSPORT_H

#include <SupportDefs.h>


// Abstract transport interface for 9P protocol
class P9Transport {
public:
	virtual				~P9Transport() {}

	// Initialize transport
	virtual status_t	Init() = 0;

	// Uninitialize transport
	virtual void		Uninit() = 0;

	// Send a complete 9P message
	virtual status_t	SendMessage(const void* data, size_t size) = 0;

	// Receive a complete 9P message
	// On entry, *size is buffer capacity
	// On exit, *size is actual message size
	virtual status_t	ReceiveMessage(void* buffer, size_t* size) = 0;

	// Get maximum message size supported by transport
	virtual size_t		MaxMessageSize() const = 0;

	// Get transport name for debugging
	virtual const char*	Name() const = 0;
};

#endif // _9P_TRANSPORT_H
