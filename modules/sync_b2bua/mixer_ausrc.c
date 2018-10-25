/**
 * @file sync_b2bua/play.c Mixer source -- playback
 *
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "sync_b2bua.h"
#include "mixer_ausrc.h"

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	ausrc_device_set_ausrc(st->dev, NULL);

	mem_deref(st->dev);
}

int mixer_ausrc_alloc(struct ausrc_st **stp, const struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct ausrc_device_st *dev;
	int err = 0;
	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	dev = ausrc_device_find(device);
	if (!dev) {
		warning("mixer_ausrc: no device found: '%s'\n", device);
		return ENOENT;
	}

	if (prm->fmt != AUFMT_S16LE) {
		warning("mixer_ausrc: unsupported sample format (%s)\n",
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
	st->dev->ausrc = st;

	mem_ref(dev);

	*stp = st;

	return err;
}
