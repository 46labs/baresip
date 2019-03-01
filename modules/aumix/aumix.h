/**
 * @file aumix/aumix.h Audio mixer -- internal interface
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

/* Audio mixer */
struct aumix *mixer;

/* Device */
extern struct hash *aumix_ht_device;

int aumix_play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg);

int aumix_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);

int aumix_device_alloc(struct device **devp, const char *name);
void aumix_device_enable(struct device *dev);
void aumix_device_disable(struct device *dev);
struct device *aumix_device_find(const char *name);
void aumix_device_set_ausrc(struct device *st, struct ausrc_st *ausrc);
struct aumix_source *aumix_device_aumix_src(struct device *dev);
