/**
 * @file commands.c
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include "sync_b2bua.h"


/**
 * Create a nosip call state object
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id         ID for the nosip call to be created
 * @param sip_callid ID of the SIP call to be connected to
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_nosip_call_create(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_id, *oe_sip_callid;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	oe_id = odict_lookup(od, "id");
	oe_sip_callid = odict_lookup(od, "sip_callid");
	if (!oe_id || !oe_sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: nosip_call_create: id='%s', sip_callid:'%s'\n",
			oe_id ? oe_id->u.str : "",
			oe_sip_callid ? oe_sip_callid->u.str : "");

	err = nosip_call_create(pf, oe_id->u.str, oe_sip_callid->u.str);

 out:
	mem_deref(od);

	return err;
}


/**
 * Connect a nosip call with the corresponding SIP call
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id         ID for the nosip call to be created
 * @param sip_callid ID of the SIP call to be connected to
 * @param desc       SDP answer
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_nosip_call_connect(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_id, *oe_sip_callid, *oe_desc;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	oe_id = odict_lookup(od, "id");
	oe_sip_callid = odict_lookup(od, "sip_callid");
	oe_desc = odict_lookup(od, "desc");
	if (!oe_id || !oe_sip_callid || !oe_desc) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: nosip_call_connect:  id='%s', sip_callid:'%s'\n",
	      oe_id ? oe_id->u.str : "", oe_sip_callid ? oe_sip_callid->u.str : "");

	err = nosip_call_connect(pf, oe_id->u.str, oe_sip_callid->u.str, oe_desc->u.str);

 out:
	mem_deref(od);

	return err;
}


/**
 * Terminate a nosip call state object
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param sip_callid ID of the SIP call to be connected to
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_sip_call_hangup(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_sip_callid, *oe_reason;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	oe_sip_callid = odict_lookup(od, "sip_callid");
	oe_reason = odict_lookup(od, "reason");

	debug("sync_b2bua: sip_call_hangup:  id='%s'\n",
	      oe_sip_callid ? oe_sip_callid->u.str : "");

	err = sip_call_hangup(pf, oe_sip_callid->u.str, oe_reason->u.str);

	mem_deref(od);

	return err;
}


static int cmd_status(struct re_printf *pf, void *arg)
{
	int err;

	(void)arg;

	err = status(pf);

	return err;
}


/**
 * Start playing a file on a SIP call
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param sip_callid ID of the SIP call
 * @param file       Name of the file to be played
 * @param loop       True if the file is to be played on loop
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_play_start(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_sip_callid, *oe_file, *oe_loop;
	bool loop;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err ;
	}

	oe_sip_callid = odict_lookup(od, "sip_callid");
	oe_file       = odict_lookup(od, "file");
	if (!oe_sip_callid || !oe_file) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	oe_loop = odict_lookup(od, "loop");
	if (oe_loop && oe_loop->type == ODICT_BOOL)
		loop = oe_loop->u.boolean;
	else
		loop = false;

	debug("sync_b2bua: play_start: sip_callid:'%s', file:'%s', loop:'%d'\n",
			oe_sip_callid ? oe_sip_callid->u.str : "",
			oe_file ? oe_file->u.str : "",
			loop);

	err = play_start(pf, oe_sip_callid->u.str, oe_file->u.str, loop);

 out:
	mem_deref(od);

	return err;
}


/**
 * Stop playing a file on a SIP call
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param sip_callid ID of the SIP call
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_play_stop(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_sip_callid;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	oe_sip_callid = odict_lookup(od, "sip_callid");
	if (!oe_sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	debug("sync_b2bua: play_stop: sip_callid:'%s'\n",
			oe_sip_callid ? oe_sip_callid->u.str : "");

	err = play_stop(pf, oe_sip_callid->u.str);

 out:
	mem_deref(od);

	return err;
}


/**
 * Get a list of SIP callids that that are currently playing a file
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_play_list(struct re_printf *pf, void *arg)
{
	int err;

	(void)arg;

	debug("sync_b2bua: play_list\n");

	err = play_list(pf);

	return err;
}


/**
 * Get the RTP capabilities of the baresip instance
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_rtp_capabilities(struct re_printf *pf, void *arg)
{
	int err;

	(void)arg;

	err = rtp_capabilities(pf);

	return err;
}


/**
 * Add a source into the mixer.
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id           ID for the nosip call to be created
 * @param [sip_callid] ID of the SIP call to be connected to
 * @param desc         SDP offer
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mixer_source_add(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_id, *oe_sip_callid, *oe_desc;
	char *sip_callid;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	oe_id = odict_lookup(od, "id");
	oe_desc = odict_lookup(od, "desc");
	if (!oe_id || !oe_desc) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	oe_sip_callid = odict_lookup(od, "sip_callid");
	if (oe_sip_callid && oe_sip_callid->type == ODICT_STRING)
		sip_callid = oe_sip_callid->u.str;
	else
		sip_callid = NULL;

	debug("sync_b2bua: mixer_source_add:  id='%s', sip_callid:'%s'\n",
	      oe_id ? oe_id->u.str : "", sip_callid ? sip_callid : "");

	err = mixer_source_add(pf, oe_id->u.str, sip_callid, oe_desc->u.str);

 out:
	mem_deref(od);

	return err;
}


/**
 * Delete a source from the mixer.
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id ID for the mixer source to be deleted
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mixer_source_del(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const struct odict_entry *oe_id;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	oe_id = odict_lookup(od, "id");
	if (!oe_id || oe_id->type != ODICT_STRING) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: mixer_source_del:  id='%s'\n",
	      oe_id ? oe_id->u.str : "");

	err = mixer_source_del(pf, oe_id->u.str);

 out:
	mem_deref(od);

	return err;
}


const struct cmd cmdv[] = {
	{"sync_b2bua_status",      0,       0, "B2UA status",   cmd_status             },
	{"play_start",             0, CMD_PRM, "Play start",    cmd_play_start         },
	{"play_stop",              0, CMD_PRM, "Play stop",     cmd_play_stop          },
	{"play_list",              0,       0, "Play list",     cmd_play_list          },
	{"sip_call_hangup",        0, CMD_PRM, "Call hangup",   cmd_sip_call_hangup    },
	{"nosip_call_create",      0, CMD_PRM, "Call create",   cmd_nosip_call_create  },
	{"nosip_call_connect",     0, CMD_PRM, "Call connect",  cmd_nosip_call_connect },
	{"nosip_rtp_capabilities", 0,       0, "Capabilities",  cmd_rtp_capabilities   },
	{"mixer_source_add",       0,       0, "Source add",    cmd_mixer_source_add   },
	{"mixer_source_del",       0,       0, "Source delete", cmd_mixer_source_del   },
};

const size_t command_count = ARRAY_SIZE(cmdv);
