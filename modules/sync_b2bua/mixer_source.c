/**
 * @file sync_b2bua/mixer_source.c
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "sync_b2bua.h"


static void mixer_source_destructor(void *arg)
{
	struct mixer_source *src = arg;

	list_unlink(&src->le);
	hash_unlink(&src->leh);

	mem_deref(src->nosip_call);
}


/**
 * Allocate a new mixer soure
 *
 * @param srcp             Pointer to allocated mixer source object
 * @param aumix            Audio mixer
 * @param device           Device name to be created
 * @param nosip_call       nosip_call associated with the mixer source
 * @param sip_call_related True if the audio source relates to a SIP call
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_mixer_source_alloc(struct mixer_source **srcp,
		   const char *device, struct nosip_call *nosip_call)
{
	struct mixer_source *src;

	debug("mixer_source_alloc [device:%s]\n", device);

	if (!device || !nosip_call)
		return EINVAL;

	src = mem_zalloc(sizeof(*src), mixer_source_destructor);
	if (!src)
		return ENOMEM;

	src->nosip_call = nosip_call;

	*srcp = src;

	return 0;
}
