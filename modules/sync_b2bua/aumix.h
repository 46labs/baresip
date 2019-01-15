/**
 * @file sync_b2bua/aumix.h Audio mixer -- internal interface
 *
 * Copyright (C) 2018 46labs
 */

#include <pthread.h>

struct device;
struct aumix;

struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct device *dev;
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void *arg;
};

struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	struct device *dev;
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	auplay_write_h *wh;
	struct auplay_prm prm;
	void *arg;
};

int sync_play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg);

int sync_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);

int sync_device_alloc(struct device **devp, struct aumix* mixer,
		   const char *name, bool enable_src);
void sync_device_enable(struct device *dev);
void sync_device_disable(struct device *dev);
struct device *sync_device_find(const char *name);
void sync_device_set_ausrc(struct device *st, struct ausrc_st *ausrc);
struct aumix_source *sync_device_aumix_src(struct device *dev);
