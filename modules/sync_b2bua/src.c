/**
 * @file src.c Audio mixer source
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

	device_set_ausrc(st->dev, NULL);
}


int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
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

	dev = device_find(device);
	if (!dev) {
		warning("aumix: no device found: '%s'\n", device);
		return ENOENT;
	}

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

	st->dev = dev;
	device_set_ausrc(st->dev, st);

	*stp = st;

	return err;
}
