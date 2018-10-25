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

static void device_destructor(void *arg)
{
	struct ausrc_device_st *st = arg;

	debug("ausrc_destructor");

	list_unlink(&st->le);
	mem_deref(st->device);
}

int ausrc_device_alloc(struct ausrc_device_st **stp, const char *device)
{
	struct le *le;
	struct ausrc_device_st *st;
	int err;

	/* Verify that a device with the same name is not already allocated */
	for ((le) = list_head((&devicel)); (le); (le) = (le)->next) {
		struct ausrc_device_st *device_st = le->data;

		if (!strcmp(device_st->device, device)) {
			warning("mixer_device: device already allocated: '%s'", device);
			return EINVAL;
		}
	}

	st = mem_zalloc(sizeof(*st), device_destructor);
	if (!st)
		return ENOMEM;

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->ausrc = NULL;

	list_append(&devicel, &st->le, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}

struct ausrc_device_st *ausrc_device_find(const char *device)
{
	struct le *le;

	for (le = devicel.head; le; le = le->next) {
		struct ausrc_device_st *st = le->data;

		if (!strcmp(st->device, device))
			return st;
	}

	return NULL;
}

void ausrc_device_set_ausrc(struct ausrc_device_st *st, struct ausrc_st *ausrc)
{
	if (!st)
		return;

	st->ausrc = ausrc;
}

struct ausrc_st *ausrc_device_get_ausrc(struct ausrc_device_st *st)
{
	if (!st)
		return NULL;

	return st->ausrc;
}
