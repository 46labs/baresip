/**
 * @file b2bua.c Back-to-Back User-Agent (B2BUA) module
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "sfu_call.h"
#include "rtp_parameters.h"

/**
 * @defgroup sfu_b2bua sfu_b2bua
 *
 * Sync Back-to-Back User-Agent (B2BUA) module
 *
 * N session objects
 * 1 session object has 2 call objects (SIP call, SFU audio object)
 */

/**
 *
 * Incoming SIP Call flow
 * ----------------------
 *
 * 1. UA_EVENT_CALL_INCOMING event.
 * 2. Module command: connect(call_id, sdp):
 *	- create the sfu_call with the given remote SDP.
 *	- connect SIP call audio with SFU call audio.
 */

struct session {
	struct le le;
	struct call *sip_call;
	struct sfu_call *sfu_call;
};

static struct list sessionl;
static struct ua *ua_in;


static void destructor(void *arg)
{
	struct session *sess = arg;

	debug("sync_b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->sip_call, sess->sfu_call);

	list_unlink(&sess->le);

	if (sess->sfu_call)
		mem_deref(sess->sfu_call);

	if (sess->sip_call)
		mem_deref(sess->sip_call);
}


static struct session *get_session_by_sip_callid(const char* id)
{
	struct le *le;

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (sess->sip_call && !strcmp(call_id(sess->sip_call), id))
			return sess;
	}

	return NULL;
}

static struct session *get_session_by_sfu_callid(const char* id)
{
	struct le *le;

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (sess->sfu_call && !strcmp(sfu_call_id(sess->sfu_call), id))
			return sess;
	}

	return NULL;
}

static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct session *sess = arg;

	switch (ev) {

	case CALL_EVENT_ESTABLISHED:
		debug("sync_b2bua: CALL_ESTABLISHED: peer_uri=%s\n",
		      call_peeruri(call));
		break;

	case CALL_EVENT_CLOSED:
		debug("sync_b2bua: CALL_CLOSED: %s\n", str);

		mem_deref(sess);
		break;

	default:
		break;
	}
}


static void call_dtmf_handler(struct call *call, char key, void *arg)
{
	(void)call;
	(void)arg;

	debug("sync_b2bua: received DTMF event: key = '%c'\n", key ? key : '.');
}


static int new_session(struct call *call)
{
	struct session *sess;

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	sess->sip_call = call;

	call_set_handlers(sess->sip_call, call_event_handler,
			  call_dtmf_handler, sess);

	ua_answer(call_get_ua(call), call);

	list_append(&sessionl, &sess->le, sess);

	return 0;
}

/**
 * Connect a SFU call with a SIP call
 *
 * @param pf        Response message print handler
 * @param arg       JSON
 *
 * @param [id]         (char*) ID for the SFU call to be created.
 * @param [sip_callid] (char*) callid of the SIP call related to this object.
 * @param [desc]       (char*) SDP answer.
 *
 * @return 0 if success, otherwise errorcode
 */
static int sfu_call_connect(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od = NULL;
	const struct odict_entry *oe_id, *oe_sip_callid, *oe_desc;
	struct mbuf *mb = NULL;
	struct session *sess;
	char a[64], b[64];
	int err;

	(void)pf;

	// retrieve command params.
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	oe_id = odict_lookup(od, "id");
	oe_sip_callid = odict_lookup(od, "sip_callid");
	oe_desc = odict_lookup(od, "sdp");
	if (!oe_id || !oe_sip_callid || !oe_desc) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	debug("sync_b2bua: sfu_cal_connect:  id='%s', sip_callid:'%s'\n",
	      oe_id ? oe_id->u.str : "", oe_sip_callid ? oe_sip_callid->u.str : "");

	// check that SFU call exist for the given id.
	sess = get_session_by_sfu_callid(oe_id->u.str);
	if (!sess) {
		warning("sync_b2bua: no session exists for the given SFU call id: %s\n",
				oe_id->u.str);
		err = EINVAL;
		goto out;
	}

	// check that SIP call exist for the given SIP callid.
	sess = get_session_by_sip_callid(oe_sip_callid->u.str);
	if (!sess) {
		warning("sync_b2bua: no session found for the given callid: %s\n",
				oe_sip_callid->u.str);
		err = EINVAL;
		goto out;
	}

	mb = mbuf_alloc(4096);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_write_mem(mb, (uint8_t *)oe_desc->u.str, str_len(oe_desc->u.str));
	if (err)
		return err;

	// create a SFU call.
	err = sfu_call_alloc(&sess->sfu_call, oe_id->u.str, false /* offer */);
	if (err) {
		warning("sync_b2bua: sfu_call_alloc failed (%m)\n", err);
		goto out;
	}

	// accept the call with the remote SDP.
	sfu_call_accept(sess->sfu_call, mb, true /* offer */);
	if (err) {
		warning("sync_b2bua: sfu_call_accept failed (%m)\n", err);
		goto out;
	}

	re_snprintf(a, sizeof(a), "A-%x", sess);
	re_snprintf(b, sizeof(b), "B-%x", sess);

	/* connect the audio/video-bridge devices */
	audio_set_devicename(call_audio(sess->sip_call), a, b);
	audio_set_devicename(sfu_call_audio(sess->sfu_call), b, a);

	// TODO: prepare response.

 out:
	if (err)
		mem_deref(sess);

	if (mb)
		mem_deref(mb);

	mem_deref(od);

	return err;
}

/**
 * Create a SFU call state object
 *
 * @param pf        Response message print handler
 * @param arg       JSON
 *
 * @param [id]         (char*) ID for the SFU call to be created.
 * @param [sip_callid] (char*) callid of the SIP call related to this object.
 *
 * @return 0 if success, otherwise errorcode
 */
static int sfu_call_create(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od = NULL;
	struct odict *od_resp = NULL;
	struct odict *od_rtp_params = NULL;
	struct odict *od_rtp_transport = NULL;
	const struct odict_entry *oe_id, *oe_sip_callid;
	struct session *sess = NULL;
	int err;

	// retrieve command params.
	err = json_decode_odict(&od, 32, param, str_len(param), 16);
	if (err) {
		warning("sync_b2bua: failed to decode JSON (%m)\n", err);
		goto out;
	}

	oe_id = odict_lookup(od, "id");
	oe_sip_callid = odict_lookup(od, "sip_callid");
	if (!oe_id || !oe_sip_callid) {
		warning("sync_b2bua: missing json entries\n");
		goto out;
	}

	debug("sync_b2bua: sfu_call_create: id='%s', sip_callid:'%s'\n",
			oe_id ? oe_id->u.str : "",
			oe_sip_callid ? oe_sip_callid->u.str : "");

	// check that no SFU call exists for the given id.
	sess = get_session_by_sfu_callid(oe_id->u.str);
	if (sess) {
		warning("sync_b2bua: session exists for the given SFU callid: %s\n",
			oe_id->u.str);
		return EINVAL;
	}

	// check that a SIP call exists for the given SIP callid.
	sess = get_session_by_sip_callid(oe_sip_callid->u.str);
	if (!sess) {
		warning("sync_b2bua: no session exist for the given SIP callid: %s\n",
				oe_sip_callid->u.str);
		return EINVAL;
	}

	// create a SFU call.
	err = sfu_call_alloc(&sess->sfu_call, oe_id->u.str, true /* offer */);
	if (err) {
		warning("sync_b2bua: sfu_call_alloc failed (%m)\n", err);
		goto out;
	}

	// TMP
	sfu_call_sdp_media_debug(sess->sfu_call);

	// prepare response.
	err = odict_alloc(&od_resp, 1024);
	if (err)
		return err;

	err |= sfu_call_get_lrtp_parameters(sess->sfu_call, &od_rtp_params);
	if (err) {
		warning("sync_b2bua: failed to retrieve rtp_parameters (%m)\n", err);
		goto out;
	}

	err |= sfu_call_get_lrtp_transport(sess->sfu_call, &od_rtp_transport);
	if (err) {
		warning("sync_b2bua: failed to retrieve rtp_transport (%m)\n", err);
		goto out;
	}

	err |= odict_entry_add(od_resp, "rtpParameters", ODICT_OBJECT, od_rtp_params);
	err |= odict_entry_add(od_resp, "rtpTransport", ODICT_OBJECT, od_rtp_transport);

	err = json_encode_odict(pf, od_resp);
	if (err) {
		warning("sync_b2bua: failed to encode json (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(sess);

	mem_deref(od);
	mem_deref(od_resp);
	mem_deref(od_rtp_params);
	mem_deref(od_rtp_transport);

	return err;
}

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	int err;
	(void)prm;
	(void)arg;

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
		debug("sync_b2bua: CALL_INCOMING: peer=%s  -->  local=%s. id=%s\n",
		      call_peeruri(call), call_localuri(call), call_id(call));

		err = new_session(call);
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		break;

	default:
		break;
	}
}


static int sfu_b2bua_status(struct re_printf *pf, void *arg)
{
	struct le *le;
	int err = 0;
	(void)arg;

	err |= re_hprintf(pf, "B2BUA status:\n");
	err |= re_hprintf(pf, "  SIP UA:  %s\n", ua_aor(ua_in));

	err |= re_hprintf(pf, "sessions:\n");

	for (le = sessionl.head; le; le = le->next) {

		struct session *sess = le->data;

		err |= re_hprintf(pf, "SIP call (%s)\n", call_peeruri(sess->sip_call));
		err |= re_hprintf(pf, "%H\n", call_status, sess->sip_call);
		err |= re_hprintf(pf, "%H\n", audio_debug, call_audio(sess->sip_call));

		err |= re_hprintf(pf, "SFU call  \n");
		err |= re_hprintf(pf, "%H\n", audio_debug, sfu_call_audio(sess->sfu_call));
	}

	return err;
}


static const struct cmd cmdv[] = {
	{"sfu_b2bua_status" , 0, 0      , "sfu_b2bua_status" , sfu_b2bua_status },
	{"sfu_call_create"  , 0, CMD_PRM, "sfu_call_create"  , sfu_call_create  },
	{"sfu_call_connect" , 0, CMD_PRM, "sfu_call_connect" , sfu_call_connect },
	// mixer_source_add(desc, [sip_call_id])
	// {"mixer_source_add" , 0, CMD_PRM, "mixer_source_add" , mixer_source_add },
	// {"mixer_source_remove" , 0, CMD_PRM, "mixer_source_remove" , mixer_source_remove },
};


static int module_init(void)
{
	int err;

	ua_in  = uag_find_param("b2bua", "inbound");

	if (!ua_in) {
		warning("sync_b2bua: inbound UA not found\n");
		return ENOENT;
	}

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	/* The inbound UA will handle all non-matching requests */
	ua_set_catchall(ua_in, true);

	debug("sync_b2bua: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("sync_b2bua: module closing..\n");

	if (!list_isempty(&sessionl)) {

		info("sync_b2bua: flushing %u sessions\n", list_count(&sessionl));
		list_flush(&sessionl);
	}

	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(b2bua) = {
	"b2bua",
	"application",
	module_init,
	module_close
};
