/**
 * @file aumix/device.c Audio mixer pseudo-device
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

static void device_destructor(void *arg)
{
	struct device *dev = arg;

	hash_unlink(&dev->le);

	mem_deref(dev->aumix_src);
	mem_deref(dev->name);
}


static bool list_apply_handler(struct le *le, void *arg)
{
	struct device *st = le->data;

	return arg && 0 == str_cmp(st->name, arg);
}


static void aumix_frame_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct device *dev = arg;
	struct ausrc_st *as;

	as = dev->ausrc;

	if (!as || !as->rh)
		return;

	as->rh(sampv, sampc, as->arg);

	/* debug("aumix_frame_handler. sampc: '%zu'\n", sampc); */
}


/**
 * Allocate a device
 *
 * @param devp        Pointer to allocated device
 * @param name        Device name
 * @param enable_src  True if ausrc needs to be created
 *
 * @return 0 if success, otherwise errorcode
 */
int aumix_device_alloc(struct device **devp, const char *name)
{
	struct device *dev;
	int err;

	debug("device_alloc [name:%s]\n", name);

	if (!devp || !mixer)
		return EINVAL;

	dev = mem_zalloc(sizeof(*dev), device_destructor);
	if (!dev)
		return ENOMEM;

	err = str_dup(&dev->name, name);
	if (err)
		return err;

	dev->mixer = mixer;

	/* Create the aumix source */
	err = aumix_source_alloc(&dev->aumix_src, mixer,
			aumix_frame_handler, dev);
	if (err) {
		warning("aumix: aumix_source_alloc failed (%m)\n", err);
		goto out;
	}

	hash_append(aumix_ht_device, hash_joaat_str(name), &dev->le, dev);

	*devp = dev;

 out:
	if (err)
		mem_deref(dev);

	return err;
}


void aumix_device_enable(struct device *dev)
{
	if (!dev)
		return;

	aumix_source_enable(dev->aumix_src, true);
}


void aumix_device_disable(struct device *dev)
{
	if (!dev)
		return;

	aumix_source_enable(dev->aumix_src, false);
}


struct device *aumix_device_find(const char *device)
{
	return list_ledata(hash_lookup(aumix_ht_device, hash_joaat_str(device),
				       list_apply_handler, (void *)device));
}


void aumix_device_set_ausrc(struct device *dev, struct ausrc_st *ausrc)
{
	if (!dev)
		return;

	/*
	 * Disable the device to avoid 'aumix_frame_handler'
	 * from being executed
	 */
	aumix_device_disable(dev);

	dev->ausrc = ausrc;

	if (ausrc)
		aumix_device_enable(dev);
}


struct aumix_source *aumix_device_aumix_src(struct device *dev)
{
	if (!dev)
		return NULL;

	return dev->aumix_src;
}