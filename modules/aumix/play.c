/**
 * @file aumix/play.c Audio mixer playback
 *
 * Copyright (C) 2018 46labs
 */
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aumix.h"


static void play_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of the thread */
	if (st->run) {
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->dev);
}


static void *write_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct auplay_st *st = arg;
	struct aumix_source *aumix_src;

	aumix_src = aumix_device_aumix_src(st->dev);

	while (st->run) {

		(void)sys_msleep(10);

		if (!st->run)
			break;

		now = tmr_jiffies();

		if (ts > now)
			continue;

		/* Write audio from rx buffer into auplay_st */
		st->wh(st->sampv, st->sampc, st->arg);

		/* Put audio from auplay_st into aumix_source */
		aumix_source_put(aumix_src, st->sampv, st->sampc);

		ts += st->prm.ptime;
	}

	return NULL;
}


int aumix_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		   struct auplay_prm *prm, const char *device,
		   auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	struct device *dev;
	int err;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aumix: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), play_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	dev = aumix_device_find(device);
	if (dev) {
		st->dev = mem_ref(dev);
	}
	else {
		err = aumix_device_alloc(&st->dev, device);
		if (err)
			return err;
	}

	aumix_device_enable(dev);

	st->run = true;
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("aumix: playback started (%s)\n", device);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
