/**
 * @file sync_b2bua/device.c Mixer source device
 *
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "sync_b2bua.h"

static struct list devicel;

struct auplay_device_st {
	struct le le;
	char* device;
	struct aumix_source *aumix_source;
};

static void device_destructor(void *arg)
{
	struct auplay_device_st *st = arg;

	list_unlink(&st->le);

	mem_deref(st->device);
}

int auplay_device_alloc(struct auplay_device_st **stp, const char *device,
		struct aumix_source *aumix_source)
{
	struct le *le;
	struct auplay_device_st *st;
	int err;

	/* Verify that a device with the same name is not already allocated */
	for ((le) = list_head((&devicel)); (le); (le) = (le)->next) {
		struct auplay_device_st *device_st = le->data;

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

	st->aumix_source = aumix_source;

	list_append(&devicel, &st->le, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}

struct auplay_device_st *auplay_device_find(const char *device)
{
	struct le *le;

	for (le = devicel.head; le; le = le->next) {
		struct auplay_device_st *st = le->data;

		if (!strcmp(st->device, device))
			return st;
	}

	return NULL;
}

struct aumix_source *auplay_device_aumix_src(struct auplay_device_st *device)
{
	if (!device)
		return NULL;

	return device->aumix_source;
}
