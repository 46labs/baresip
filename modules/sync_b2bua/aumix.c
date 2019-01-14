/**
 * @file sync_b2bua/aumix.c Audio mixer virtual driver
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <baresip.h>
#include "aumix.h"

static struct ausrc *ausrc;
static struct auplay *auplay;


static int module_init(void)
{
	int err;

	err = ausrc_register(&ausrc, baresip_ausrcl(), "aumix",
			 src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "aumix",
			 play_alloc);

	return err;
}


static int module_close(void)
{
	mem_deref(auplay);
	mem_deref(ausrc);

	return 0;
}


const struct mod_export DECL_EXPORTS(aumix) = {
	"aumix",
	"audio",
	module_init,
	module_close
};

