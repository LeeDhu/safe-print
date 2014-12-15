/**
 * @file musb.c
 * @brief backend of printer
 * @author Li Ming <Lee.dhucst@gmail.com>
 * @url www.freereaper.com
 * @version v1.0.0
 * @date 2014-12-09
 * @Copyright (c) DHU, CST.  All Rights Reserved
 */

#include <unistd.h>
#include "common.h"
#include "log.h"

#include "musb.h"


static libusb_context *libusb_ctx = NULL;
static libusb_device **device_list;

static app_status_t get_libusb_device(struct libusb_component *libusb_component)
{
	libusb_device *device = NULL; /* Current device */
	struct libusb_device_descriptor devdesc; /* Current device descriptor */
	struct libusb_config_descriptor *confptr = NULL; /* Pointer to current configuration */
	const struct libusb_interface *ifaceptr = NULL; /* Pointer to current interface */
	const struct libusb_interface_descriptor *altptr = NULL; /* Pointer to current alternate setting */
	int numdevs = 0;        /* number of connected devices */
	int i, conf, iface, altset ;
	int ret = 0;

	ret = libusb_init(&libusb_ctx);
	if (ret) {
		sys_log(LOGS_ERROR, "unable to initialize usb access via libusb,"
				"libusb error %d\n", ret);
		return APP_USB_ERR;
	}

	numdevs = libusb_get_device_list(libusb_ctx, &device_list);
	if (numdevs > 0) {
		for (i=0; i< numdevs; i++) {
			device = device_list[i];
			memset(&devdesc, 0, sizeof(devdesc));

			if (libusb_get_device_descriptor (device, &devdesc) < 0) {
				continue;
			}

			if (!devdesc.bNumConfigurations || !devdesc.idVendor || !devdesc.idProduct) {
				continue;
			}

			/*Not a HP device*/
			if (devdesc.idVendor != 0x3f0) {
				continue;
			}

			for (conf = 0; conf < devdesc.bNumConfigurations; conf++) {
				if (libusb_get_config_descriptor (device, conf, &confptr) < 0) {
					continue;
				}
				for (iface = 0, ifaceptr = confptr->interface;
					iface < confptr->bNumInterfaces;
				   	iface ++, ifaceptr ++) {
					for (altset = 0, altptr = ifaceptr->altsetting;
						 altset < ifaceptr->num_altsetting;
						 altset++, altptr++) {
						if ((altptr->bInterfaceClass == LIBUSB_CLASS_PRINTER)
							   	 && (altptr->bInterfaceSubClass == 1)
								 && (altptr->bInterfaceProtocol == 2)) {
 								/* found printer device */
								libusb_component->device      = device;
								libusb_component->config      = conf;
								libusb_component->interface   = iface;
								libusb_component->alt_setting = altset;
								libusb_free_config_descriptor(confptr);
								return APP_STATUS_OK;
						}
					}
				}
				libusb_free_config_descriptor(confptr); confptr = NULL;
			}//end for conf
		}
	}
bugout:
	if (confptr != NULL) {
		libusb_free_config_descriptor(confptr);
	}
	return APP_NODEV_ERR;
}

static int get_ep(libusb_device *dev, int config, int interface, int altset, enum libusb_transfer_type type, enum libusb_endpoint_direction epdir)
{
	struct libusb_config_descriptor *confptr = NULL;
	const struct libusb_interface_descriptor *pi;
	int i, endpoint = -1;

	if (libusb_get_config_descriptor(dev, config, &confptr) != 0)
		goto bugout;

	if (confptr == NULL || confptr->interface == NULL || confptr->interface[interface].altsetting == NULL)
		goto bugout;

	pi = &(confptr->interface[interface].altsetting[altset]);
	for (i=0; i<pi->bNumEndpoints; i++)
	{
		if (pi->endpoint == NULL)
			goto bugout;
		if (pi->endpoint[i].bmAttributes == type)
		{
			if (epdir == LIBUSB_ENDPOINT_IN)
			{
				if (pi->endpoint[i].bEndpointAddress & LIBUSB_ENDPOINT_IN)
				{
					endpoint = pi->endpoint[i].bEndpointAddress;
					break;
				}
			}
			else if (epdir == LIBUSB_ENDPOINT_OUT)
			{
				if (!(pi->endpoint[i].bEndpointAddress & LIBUSB_ENDPOINT_IN))
				{
					endpoint = pi->endpoint[i].bEndpointAddress;
					break;
				}
			}
		}
	}
	//DBG("get_ep(bmAttributes=%x): bEndpointAddress=%x interface=%x\n", type, endpoint, interface);
bugout:
	libusb_free_config_descriptor(confptr);
	if (endpoint == -1) {
		sys_log(LOGS_ERROR, "get_ep: ERROR! returning -1\n");
	}
	return endpoint; /* no endpoint found */
}

static int get_in_ep(libusb_device *dev, int config, int interface, int altset, enum libusb_transfer_type type)
{
	return get_ep(dev, config, interface, altset, type, LIBUSB_ENDPOINT_IN);
}

static int get_out_ep(libusb_device *dev, int config, int interface, int altset, enum libusb_transfer_type type)
{
	return get_ep(dev, config, interface, altset, type, LIBUSB_ENDPOINT_OUT);
}

static void clear_ep_halt(struct libusb_component *com)
{
	int ep = -1;

	if (( ep = get_in_ep(com->device, com->config, com->interface, com->alt_setting, LIBUSB_TRANSFER_TYPE_BULK)) >= 0) {
		libusb_clear_halt(com->handle,  ep);
	}

	if (( ep = get_out_ep(com->device, com->config, com->interface, com->alt_setting, LIBUSB_TRANSFER_TYPE_BULK)) >= 0) {
		libusb_clear_halt(com->handle,  ep);
	}

}

static int claim_interface(struct libusb_component *com)
{
	int ret = 0;
	struct libusb_device *device = com->device;
	int iface = com->interface;
	int altsetting = com->alt_setting;

	if (ret = libusb_open(device, &com->handle)) {
		sys_log(LOGS_ERROR, "failed to open device, error code: %d\n", ret);
	}

	if (ret = libusb_claim_interface(com->handle, iface)) {
		libusb_close(com->handle);
		fprintf(stderr, "unable to claim interface\n");
		com->handle = NULL;
	}

	if (ret = libusb_set_interface_alt_setting(com->handle, iface, altsetting)) {
		libusb_release_interface(com->handle, iface);
		libusb_close(com->handle);
		com->handle = NULL;
		fprintf(stderr, "unable to set interface\n");
	}

	return ret;
}

app_status_t open_device(struct libusb_component  *libusb_component)
{
	int ret = 0;

	ret = get_libusb_device(libusb_component);
	if (ret) {
		sys_log(LOGS_WARNING, "unable to find usb printer device \n");
		return APP_NODEV_ERR;
	}

	ret = claim_interface(libusb_component);
	if (ret) {
		sys_log(LOGS_WARNING, "unable to clain interface\n");
		return APP_NODEV_ERR;
	}

	return APP_STATUS_OK;
	
}


int device_read(struct libusb_component *com, void *buff, int size, int usec)
{
	int read_bytes = 0;
	int ep = -1;
	int ret = 0;

	ep = get_in_ep(com->device, com->config, com->interface, com->alt_setting, LIBUSB_TRANSFER_TYPE_BULK);
	if (ep < 0) {
		sys_log(LOGS_ERROR, "invalid bulk in endpoint\n");
		return read_bytes;
	}

	while(read_bytes == 0) {
		libusb_bulk_transfer(com->handle, ep, (unsigned char *)buff, size, &read_bytes, usec);
		sleep(1);
	}

	return read_bytes;

}

int device_write(struct libusb_component *com, const void *buff, int size, int usec)
{
	int write_bytes = 0;
	int ep = -1;

	ep = get_out_ep(com->device, com->config, com->interface, com->alt_setting, LIBUSB_TRANSFER_TYPE_BULK);

//	printf("%s, %d\n", __FUNCTION__, __LINE__);

	if (ep < 0) {
		sys_log(LOGS_ERROR, "invalid bulk out endpoint\n");
		return write_bytes;
	}


	libusb_bulk_transfer(com->handle, ep, (unsigned char *)buff, size, &write_bytes, usec);

//	printf("%s, %d\n", __FUNCTION__, __LINE__);

	return write_bytes;

}

app_status_t close_device(struct libusb_component *libusb_component)
{
	clear_ep_halt(libusb_component);

	libusb_release_interface(libusb_component->handle, libusb_component->interface);
	libusb_close(libusb_component->handle);
	libusb_component->handle = NULL;

	libusb_free_device_list(device_list, 1);
	libusb_exit(libusb_ctx);
	libusb_ctx  = NULL;
	device_list = NULL;

	return APP_STATUS_OK;
}
