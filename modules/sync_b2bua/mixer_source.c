/**
 * @file mixer_source.c noSIP call state
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "sync_b2bua.h"
#include "mixer_ausrc.h"

static void mixer_source_destructor(void *arg)
{
	struct mixer_source *src = arg;

	list_unlink(&src->le);

	mem_deref(src->nosip_call);
	mem_deref(src->ausrc_device);
	mem_deref(src->auplay_device);
	mem_deref(src->aumix_source);
}

/*
 * Write mixed audio into SIP call audio source.
 */
static void aumix_frame_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct ausrc_device_st *st = arg;
	struct ausrc_st *as;

	as = st->ausrc;

	if (!as)
		return;

	as->rh(sampv, sampc, as->arg);

	/* debug("aumix_frame_handler. sampc: '%zu'\n", sampc); */
}

/*
 * Write mixed audio into SIP call audio source.
 */
static void fake_aumix_frame_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	(void)sampv;
	(void)sampc;
	(void)arg;

	/* debug("fake_aumix_frame_handler. sampc: '%zu'\n", sampc); */
}

/**
 * Allocate a new mixer soure
 *
 * @param srcp    Pointer to allocated nosip Call state object
 * @param aumix   Audio mixer
 * @param audio   Audio state to which provide the mixed audio.
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

	/* Create mixer source */
	if (!sip_call_audio)
	{
		// This source does not need to handle the mixed audio.
		err = aumix_source_alloc(
				&src->aumix_source, aumix, fake_aumix_frame_handler, NULL);
		if (err) {
			warning("sync_b2bua: aumix_source_alloc failed (%m)\n", err);
			goto out;
		}
	}
	else
	{
		/* Allocate a mixer auplay device */
		err = ausrc_device_alloc(&src->ausrc_device, device);
		if (err) {
			warning("mixer_source: ausrc_device_alloc failed (%m)\n", err);
			goto out;
		}

		/* Create aumix source */
		err = aumix_source_alloc(
				&src->aumix_source, aumix, aumix_frame_handler, src->ausrc_device);
		if (err)
			goto out;
	}

	/* Allocate a mixer auplay device */
	err = auplay_device_alloc(&src->auplay_device, device, src->aumix_source);
	if (err) {
		warning("mixer_source: auplay_device_alloc failed (%m)\n", err);
		goto out;
	}

	*srcp = src;

 out:
	if (err) {
		mem_deref(src);
	}

	return 0;
}
