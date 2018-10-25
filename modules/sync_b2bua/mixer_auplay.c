/**
 * @file sync_b2bua/play.c Mixer source -- playback
 *
 */
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "sync_b2bua.h"

struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm prm;
	char *device;
	struct aumix_source *aumix_source;
};

static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of the thread */
	if (st->run)
	{
		debug("mixer_play: stopping playback thread (%s)\n", st->device);
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}


static void *write_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct auplay_st *st = arg;

	while (st->run) {

		(void)sys_msleep(4);

		if (!st->run)
			break;

		now = tmr_jiffies();

		if (ts > now)
			continue;

		/* Write audio from rx buffer into auplay_st */
		st->wh(st->sampv, st->sampc, st->arg);

		/* Put audio from auplay_st into aumix_source */
		aumix_source_put(st->aumix_source, st->sampv, st->sampc);

		ts += st->prm.ptime;
	}

	return NULL;
}

int mixer_auplay_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	struct auplay_device_st *dev;
	int num_frames;
	int err;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	dev = auplay_device_find(device);
	if (!dev) {
		warning("mixer_play: no device found: '%s'\n", device);
		return ENOENT;
	}

	if (prm->fmt != AUFMT_S16LE) {
		warning("mixer_play unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->aumix_source = auplay_device_aumix_src(dev);
	if (!st->aumix_source) {
		warning("mixer_play: device has no aumix source (%s)\n", device);
		return ENOTSUP;
	}

	aumix_source_enable(st->aumix_source, true);

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->ap  = ap;
	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;

	warning("------------\n");
	warning("srate: %d, ch: %zu, ptime: %zu\n",
			st->prm.srate, st->prm.ch, st->prm.ptime);
	warning("------------\n");

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	num_frames = st->prm.srate * st->prm.ptime / 1000;

	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("mixer: playback started (%s)\n", st->device);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
