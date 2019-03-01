/**
 * @file aumix/aumix.c Audio mixer virtual driver
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aumix.h"

/**
 *
 \verbatim

 .        .--------.   .-------.   .-------.
 |        |        |   |       |   |       |
 | RTP -->| auplay |-->| aumix |-->| ausrc |---> RTP
 |        |        |   |       |   |       |
 '        '--------'   '-------'   '-------'

 *
 */

enum {
	HASH_SIZE = 256,
	MAX_FILE_PATH_LENGTH = 256,
};

static struct ausrc *ausrc;
static struct auplay *auplay;

struct hash *aumix_ht_device;

/**
 * Play an audio file into the mixer.
 *
 * @param file  Name of the file to be played
 *
 * @return 0 if success, otherwise error code
 */
static int cmd_aumix_playfile(struct re_printf *pf, void *arg)
{
	(void)pf;

	const struct cmd_arg *carg = arg;
	const char *file = carg->prm;

	struct config *cfg = conf_config();
	char filepath[MAX_FILE_PATH_LENGTH];
	int err;

	(void)re_snprintf(filepath, sizeof(filepath), "%s/%s",
				cfg->audio.audio_path, file);

	err = aumix_playfile(mixer, filepath);
	if (err)
		warning("aumix: aumix_playfile failed (%m)\n", err);

	return err;
}

static const struct cmd cmdv[] = {
	{"aumix_playfile", 0, CMD_PRM, "Mixer play file", cmd_aumix_playfile },
};

static int module_init(void)
{
	struct config *cfg = conf_config();
	uint32_t srate = cfg->audio.srate_play;
	int err;

	/* Allocate device hash table */
	err = hash_alloc(&aumix_ht_device, HASH_SIZE);
	if (err)
		return err;

	err = ausrc_register(&ausrc, baresip_ausrcl(), "aumix",
			  aumix_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "aumix",
			  aumix_play_alloc);

	err |= cmd_register(baresip_commands(), cmdv, 1);

	/* Start audio mixer */
	err = aumix_alloc(&mixer, srate ? srate : 48000,
			  1 /* channels */, 20 /* ptime */);

	return err;
}


static int module_close(void)
{
	mem_deref(auplay);
	mem_deref(ausrc);

	auplay = NULL;
	ausrc = NULL;

	aumix_ht_device = mem_deref(aumix_ht_device);

	mem_deref(mixer);

	return 0;
}


const struct mod_export DECL_EXPORTS(aumix) = {
	"aumix",
	"audio",
	module_init,
	module_close
};
