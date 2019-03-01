/**
 * @file aumix/src.c Audio mixer source
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aumix.h"


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	aumix_device_set_ausrc(st->dev, NULL);

	mem_deref(st->dev);
}


int aumix_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct media_ctx **ctx,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct device *dev;
	int err = 0;
	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aumix: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as   = as;
	st->prm  = *prm;
	st->rh   = rh;
	st->arg  = arg;

	dev = aumix_device_find(device);
	if (dev) {
		st->dev = mem_ref(dev);
	}
	else {
		err = aumix_device_alloc(&st->dev, device);
		if (err)
			return err;
	}

	aumix_device_set_ausrc(st->dev, st);

	*stp = st;

	return err;
}
