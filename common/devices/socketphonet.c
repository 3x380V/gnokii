/*

  G N O K I I

  A Linux/Unix toolset and driver for the mobile phones.

  This file is part of gnokii.

  Copyright (C) 2010 Daniele Forsi

  This file provides an API for accessing functions via the phonet Linux kernel module.
  See README for more details on supported mobile phones.

  The various routines are called socketphonet_(whatever).

*/

#include "config.h"
#include "compat.h" /* for __ptr_t definition */
#include "gnokii.h"

#ifndef HAVE_SOCKETPHONET

void* socketphonet_open(gn_config *cfg, int with_odd_parity, int with_async)
{
	return NULL;
}

void socketphonet_close(void *instance) { }

size_t socketphonet_read(void *instance, __ptr_t buf, size_t nbytes)
{
	return -1;
}

size_t socketphonet_write(void *instance, const __ptr_t buf, size_t n)
{
	return -1;
}

int socketphonet_select(void *instance, struct timeval *timeout)
{
	return -1;
}

#else

/* System header files */
#include <sys/socket.h>
#include <linux/phonet.h>

/* Various header files */
#include "compat.h"
#include "links/fbus-common.h"
#include "links/fbus-phonet.h"
#include "device.h"
#include "devices/serial.h"
#include "gnokii-internal.h"

static struct sockaddr_pn addr = { .spn_family = AF_PHONET, .spn_dev = FBUS_DEVICE_PHONE };

void* socketphonet_open(gn_config *cfg, int with_odd_parity, int with_async)
{
	int fd, retcode, *data;

	fd = socket(PF_PHONET, SOCK_DGRAM, 0);
	if (fd == -1) {
		perror("socket");
		return NULL;
	}

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("bind");
		close(fd);
		return NULL;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, cfg->port_device, strlen(cfg->port_device))) {
		perror("setsockopt");
		close(fd);
		return NULL;
	}

	/* Make filedescriptor asynchronous. */

	/* We need to supply FNONBLOCK (or O_NONBLOCK) again as it would get reset
	 * by F_SETFL as a side-effect!
	 */
#ifdef FNONBLOCK
	retcode = fcntl(fd, F_SETFL, (with_async ? FASYNC : 0) | FNONBLOCK);
#else
#  ifdef FASYNC
	retcode = fcntl(fd, F_SETFL, (with_async ? FASYNC : 0) | O_NONBLOCK);
#  else
	retcode = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (retcode != -1)
		retcode = ioctl(fd, FIOASYNC, &with_async);
#  endif
#endif
	if (retcode == -1) {
		perror("fcntl");
		close(fd);
		return NULL;
	}

	data = malloc(sizeof(int));
	if (data == NULL) {
		close(fd);
		return NULL;
	}
	*data = fd;

	return data;
}

void socketphonet_close(void *instance)
{
	close(*(int *)instance);
}


size_t socketphonet_read(void *instance, __ptr_t buf, size_t nbytes)
{
	int received;
	unsigned char *frame = buf;

	received = recvfrom(*(int *)instance, buf + 8, nbytes - 8, 0, NULL, NULL);
	if (received == -1) {
		perror("recvfrom");
		return -1;
	}

	/* Hack!!! Rebuild header as expected by phonet_rx_statemachine() */
	/* Need to add 2 bytes to re-add spn_obj and spn_dev that the kernel driver doesn't return as payload */
	received += 2;
	frame[0] = FBUS_PHONET_DKU2_FRAME_ID;
	frame[1] = FBUS_PHONET_BLUETOOTH_DEVICE_PC;
	frame[2] = FBUS_DEVICE_PHONE;
	frame[3] = addr.spn_resource;
	frame[4] = received >> 8;
	frame[5] = received & 0xff;
	frame[6] = addr.spn_obj;
	frame[7] = addr.spn_dev;
	return received + 8;
}

size_t socketphonet_write(void *instance, const __ptr_t buf, size_t n)
{
	int sent;
	const unsigned char *frame = buf;

	addr.spn_resource = frame[3];

	sent = sendto(*(int *)instance, buf + 8, n - 8, 0, (struct sockaddr *)&addr, sizeof(addr));
	if (sent == -1) {
		perror("sendto");
		return -1;
	}

	return sent + 8;
}

int socketphonet_select(void *instance, struct timeval *timeout)
{
	return unix_select(*(int *)instance, timeout);
}

#endif /* HAVE_SOCKETPHONET */
