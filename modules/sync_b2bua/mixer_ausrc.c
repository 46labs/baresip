/**
 * @file sync_b2bua/play.c Mixer source -- playback
 *
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "sync_b2bua.h"

static struct list devicel;

struct device {
	struct le le;
	char *device;
	struct ausrc_st *ausrc;
};

struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct device *dev;
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void *arg;
};

static void device_destructor(void *arg)
{
	struct device *st = arg;

	warning("ausrc_destructor");

	list_unlink(&st->le);
	mem_deref(st->device);
	st->ausrc = NULL;
}

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	warning("ausrc_destructor");

	mem_deref(st->dev);
}

static struct device *device_find(const char *device)
{
	struct le *le;

	for (le = devicel.head; le; le = le->next) {
		struct device *st = le->data;

		if (!strcmp(st->device, device))
			return st;
	}

	return NULL;
}

int mixer_ausrc_alloc(struct ausrc_st **stp, const struct ausrc *as,
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
		warning("mixer_ausrc: no device found: '%s'\n", device);
		return ENOENT;
	}

	if (prm->fmt != AUFMT_S16LE) {
		warning("aubridge: source: unsupported sample format (%s)\n",
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
	dev->ausrc = st;

	*stp = st;

	return err;
}
