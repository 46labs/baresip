/**
 * @file device.c. Audio mixer pseudo-device
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aumix.h"


struct device {
	struct le le;
	struct ausrc_st *ausrc;
	struct auplay_st *auplay;
	struct aumix *mixer;
	struct aumix_source *aumix_src;
	char *name;
};


static struct list devicel;


static void device_destructor(void *arg)
{
	struct device *dev = arg;

	list_unlink(&dev->le);

	mem_deref(dev->aumix_src);
	mem_deref(dev->name);

	dev->ausrc = NULL;
	dev->auplay = NULL;
}


static void aumix_frame_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct device *dev = arg;
	struct ausrc_st *as;

	as = dev->ausrc;

	if (!as)
		return;

	as->rh(sampv, sampc, as->arg);

	/* debug("aumix_frame_handler. sampc: '%zu'\n", sampc); */
}


/**
 * Allocate a device
 *
 * @param devp    Pointer to allocated device
 * @param aumix   Audio mixer
 * @param device  Device name
 * @param source  True if ausrc needs to be created
 *
 * @return 0 if success, otherwise errorcode
 */
int device_alloc(struct device **devp, struct aumix *mixer,
		   const char *name, bool enable_src)
{
	struct device *dev;
	int err;

	debug("device_alloc [name:%s]\n", name);

	if (!devp || !mixer)
		return EINVAL;

	dev = mem_zalloc(sizeof(*dev), device_destructor);
	if (!dev)
		return ENOMEM;

	str_dup(&dev->name, name);
	dev->mixer = mixer;

	/* Create the aumix source */
	err = aumix_source_alloc(&dev->aumix_src, mixer,
			enable_src ? aumix_frame_handler : NULL,
			enable_src ? dev : NULL);
	if (err) {
		warning("aumix: aumix_source_alloc failed (%m)\n", err);
		goto out;
	}

	list_append(&devicel, &dev->le, dev);

	*devp = dev;

 out:
	if (err)
		mem_deref(dev);

	return err;
}


int device_enable(struct device *dev)
{
	if (!dev)
		return ENOENT;

	aumix_source_enable(dev->aumix_src, true);

	return 0;
}


int device_disable(struct device *dev)
{
	if (!dev)
		return ENOENT;

	aumix_source_enable(dev->aumix_src, false);

	return 0;
}


struct device *device_find(const char *name)
{
	struct le *le;

	for (le = devicel.head; le; le = le->next) {
		struct device *dev = le->data;

		if (!strcmp(dev->name, name))
			return dev;
	}

	return NULL;
}


void device_set_ausrc(struct device *dev, struct ausrc_st *ausrc)
{
	if (!dev)
		return;

	/*
	 * Disable the device to avoid 'aumix_frame_handler'
	 * from being executed
	 */
	device_disable(dev);

	dev->ausrc = ausrc;

	if (ausrc)
		device_enable(dev);
}


struct aumix_source *device_aumix_src(struct device *dev)
{
	if (!dev)
		return NULL;

	return dev->aumix_src;
}
