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
	const char *id, *sip_callid;
	struct odict *od_resp = NULL;
	struct mbuf *mb = NULL;
	char *desc = NULL;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	id = odict_string(od, "id");
	sip_callid = odict_string(od, "sip_callid");
	if (!id || !sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: nosip_call_create: id='%s', sip_callid:'%s'\n",
			id, sip_callid);

	/* Create nosip call */
	err = nosip_call_create(&mb, id, sip_callid);
	if (err) {
		warning("sync_b2bua: nosip_call_create failed (%m)\n", err);
		goto out;
	}

	/* Prepare response */
	err = odict_alloc(&od_resp, 1);
	if (err)
		goto out;

	err |= mbuf_strdup(mb, &desc, mb->end);
	err |= odict_entry_add(od_resp, "desc", ODICT_STRING, desc);
	if (err)
		goto out;

	err = json_encode_odict(pf, od_resp);
	if (err) {
		warning("sync_b2bua: json_encode_odict failed (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(od);
	mem_deref(od_resp);
	mem_deref(mb);
	mem_deref(desc);

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
	const char *id, *sip_callid, *desc;
	struct mbuf *mb = NULL;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	id = odict_string(od, "id");
	sip_callid = odict_string(od, "sip_callid");
	desc = odict_string(od, "desc");
	if (!id || !sip_callid || !desc) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: nosip_call_connect:  id='%s', sip_callid:'%s'\n",
	      id, sip_callid);

	/* Copy the SDP string into a memory buffer */
	mb = mbuf_alloc(str_len(desc));
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_write_str(mb, desc);
	if (err)
		goto out;

	/* Connect the nosip_call() */
	err = nosip_call_connect(id, sip_callid, mb);
	if (err)
		goto out;

 out:
	mem_deref(od);
	mem_deref(mb);

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
	const char *sip_callid, *reason;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	sip_callid = odict_string(od, "sip_callid");
	if (!sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	reason = odict_string(od, "reason");

	debug("sync_b2bua: sip_call_hangup: id='%s', reason='%s'\n",
	      sip_callid, reason);

	err = sip_call_hangup(sip_callid, reason);
	if (err)
		goto out;

 out:
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
	const char *sip_callid, *file;
	bool loop = false;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	sip_callid = odict_string(od, "sip_callid");
	file = odict_string(od, "file");
	if (!sip_callid || !file) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	odict_get_boolean(od, &loop, "loop");

	debug("sync_b2bua: play_start: sip_callid:'%s',"
			" file:'%s', loop:'%d'\n",
			sip_callid, file, loop);

	err = play_start(sip_callid, file, loop);

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
	const char *sip_callid;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	sip_callid = odict_string(od, "sip_callid");
	if (!sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	debug("sync_b2bua: play_stop: sip_callid:'%s'\n",
			sip_callid);

	err = play_stop(sip_callid);

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
	struct odict *od_resp, *od_array;
	int err;

	(void)pf;
	(void)arg;

	err = odict_alloc(&od_resp, 1);
	err |= odict_alloc(&od_array, (uint32_t)session_count());
	if (err)
		goto out;

	err = odict_entry_add(od_resp, "list", ODICT_ARRAY, od_array);
	if (err)
		goto out;

	debug("sync_b2bua: play_list\n");

	err = play_list(od_array);
	if (err) {
		warning("sync_b2bua: play_list failed (%m)\n", err);
		goto out;
	}

	err = json_encode_odict(pf, od_resp);
	if (err) {
		warning("sync_b2bua: failed to encode json (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(od_resp);
	mem_deref(od_array);

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
	const char *id, *sip_callid, *desc;
	struct mbuf *offer = NULL, *answer = NULL;
	int err;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	id = odict_string(od, "id");
	desc = odict_string(od, "desc");
	if (!id || !desc) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	sip_callid = odict_string(od, "sip_callid");

	debug("sync_b2bua: mixer_source_add:  id='%s', sip_callid:'%s'\n",
	      id, sip_callid);

	/* Copy the SDP offer string into a memory buffer */
	offer = mbuf_alloc(str_len(desc));
	if (!offer) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_write_str(offer, desc);
	if (err)
		goto out;

	err = mixer_source_add(&answer, id, sip_callid, offer);
	if (err) {
		warning("sync_b2bua: mixer_source_add failed (%m)\n", err);
		goto out;
	}

	/* Prepare the response */
	err = re_hprintf(pf, "%b", answer->buf, answer->end);
	if (err) {
		warning("sync_b2bua: re_hprintf failed (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(od);
	mem_deref(offer);
	mem_deref(answer);

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
	const char *id;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	id = odict_string(od, "id");
	if (!id) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: mixer_source_del:  id='%s'\n",
	      id);

	err = mixer_source_del(id);

 out:
	mem_deref(od);

	return err;
}


/**
 * Enable a mixer source.
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id           ID for the nosip call to be created
 * @param [sip_callid] ID of the SIP call source of the audio
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mixer_source_enable(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const char *id, *sip_callid;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	id = odict_string(od, "id");
	if (!id) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	sip_callid = odict_string(od, "sip_callid");

	debug("sync_b2bua: mixer_source_enable:  id='%s', sip_callid:'%s'\n",
	      id, sip_callid);

	err = mixer_source_enable(id, sip_callid);
	if (err) {
		warning("sync_b2bua: mixer_source_enable failed (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(od);

	return err;
}


/**
 * Disable a mixer source.
 *
 * @param pf  Print handler for debug output
 * @param arg JSON containing command arguments
 *
 * @param id ID for the mixer source to be deleted
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mixer_source_disable(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const char *id;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	id = odict_string(od, "id");
	if (!id) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: mixer_source_disable:  id='%s'\n",
	      id);

	err = mixer_source_disable(id);

 out:
	mem_deref(od);

	return err;
}

/**
 * Play an audio file into the mixer.
 *
 * @param file  Name of the file to be played
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_mixer_play(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od;
	const char *file;
	int err;

	(void)pf;

	/* Retrieve command params */
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		return err;
	}

	file = odict_string(od, "file");
	if (!file) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	err = mixer_play(file);

 out:
	mem_deref(od);

	return err;
}


const struct cmd cmdv[] = {
	{"sync_b2bua_status",
		0,       0, "B2UA status",        cmd_status               },
	{"play_start",
		0, CMD_PRM, "Play start",         cmd_play_start           },
	{"play_stop",
		0, CMD_PRM, "Play stop",          cmd_play_stop            },
	{"play_list",
		0,       0, "Play list",          cmd_play_list            },
	{"sip_call_hangup",
		0, CMD_PRM, "Call hangup",        cmd_sip_call_hangup      },
	{"nosip_call_create",
		0, CMD_PRM, "Call create",        cmd_nosip_call_create    },
	{"nosip_call_connect",
		0, CMD_PRM, "Call connnnect",     cmd_nosip_call_connect   },
	{"nosip_rtp_capabilities",
		0,       0, "RTP capababilities", cmd_rtp_capabilities     },
	{"mixer_source_add",
		0, CMD_PRM, "Mixer source add",   cmd_mixer_source_add     },
	{"mixer_source_del",
		0, CMD_PRM, "Mixer source del.",  cmd_mixer_source_del     },
	{"mixer_source_enable",
		0, CMD_PRM, "Mixer source en.",   cmd_mixer_source_enable  },
	{"mixer_source_disable",
		0, CMD_PRM, "Mixser source dis.", cmd_mixer_source_disable },
	{"mixer_play",
		0, CMD_PRM, "Mixer play",         cmd_mixer_play           },
};

const size_t command_count = ARRAY_SIZE(cmdv);
