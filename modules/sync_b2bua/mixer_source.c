/**
 * @file mixer_source.c
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "sync_b2bua.h"
#include "aumix.h"

static void mixer_source_destructor(void *arg)
{
	struct mixer_source *src = arg;

	list_unlink(&src->le);

	mem_deref(src->nosip_call);
	mem_deref(src->dev);
}

/**
 * Allocate a new mixer soure
 *
 * @param srcp             Pointer to allocated mixer source object
 * @param aumix            Audio mixer
 * @param device           Device name to be created
 * @param nosip_call       nosip_call associated with the mixer source
 * @param [sip_call_audio] Audio state of the related SIP call
 *
 * @return 0 if success, otherwise errorcode
 */
int mixer_source_alloc(struct mixer_source **srcp, struct aumix *aumix, char *device, struct nosip_call *nosip_call, struct audio* sip_call_audio)
{
	struct mixer_source *src;
	int err;

	debug("mixer_source_alloc [device:%s]\n", device);

	if (!device || !nosip_call)
		return EINVAL;

	src = mem_zalloc(sizeof(*src), mixer_source_destructor);
	if (!src)
		return ENOMEM;

	src->nosip_call = nosip_call;

	/* Create aumix device */
	err = device_alloc(&src->dev, aumix, device, sip_call_audio ? true : false);
	if (err)
		goto out;

	*srcp = src;

 out:
	if (err) {
		mem_deref(src);
	}

	return 0;
}
