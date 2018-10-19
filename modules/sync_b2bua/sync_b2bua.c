/**
 * @file b2bua.c Back-to-Back User-Agent (B2BUA) module
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "nosip_call.h"

/**
 * @defgroup sync_b2bua sync_b2bua
 *
 * Sync Back-to-Back User-Agent (B2BUA) module
 *
 * N session objects
 * 1 session object has 2 call objects (SIP call, noSIP call)
 */

/**
 *
 * Incoming SIP Call flow
 * ----------------------
 *
 * 1. UA_EVENT_CALL_INCOMING event.
 * 2. Module command: connect(call_id, sdp):
 *	- create the nosip_call with the given remote SDP.
 *	- connect SIP call audio with nosip call audio.
 */

struct session {
	struct le le;
	struct call *sip_call;
	struct nosip_call *nosip_call;
};

static struct list sessionl;
static struct ua *ua_in;


static void destructor(void *arg)
{
	struct session *sess = arg;

	debug("sync_b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->sip_call, sess->nosip_call);

	list_unlink(&sess->le);

	if (sess->nosip_call)
		mem_deref(sess->nosip_call);

	if (sess->sip_call)
		mem_deref(sess->sip_call);
}


static struct session *get_session_by_sip_callid(const char* id)
{
	struct le *le;

	debug("get_session_by_sip_callid(%s)\n", id);

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (sess->sip_call && !strcmp(call_id(sess->sip_call), id))
			return sess;
	}

	return NULL;
}

static struct session *get_session_by_nosip_callid(const char* id)
{
	struct le *le;

	debug("get_session_by_nosip_callid(%s)\n", id);

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (sess->nosip_call && !strcmp(nosip_call_id(sess->nosip_call), id))
			return sess;
	}

	return NULL;
}

static int new_session(struct call *call)
{
	struct session *sess;

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	mem_ref(call);

	sess->sip_call = call;

	ua_answer(call_get_ua(call), call);

	list_append(&sessionl, &sess->le, sess);

	return 0;
}

/**
 * Connect a nosip call with a SIP call
 *
 * @param pf        Response message print handler
 * @param arg       JSON
 *
 * @param [id]         (char*) ID for the nosip call to be created.
 * @param [sip_callid] (char*) callid of the SIP call related to this object.
 * @param [desc]       (char*) SDP answer.
 *
 * @return 0 if success, otherwise errorcode
 */
static int nosip_call_connect(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od = NULL;
	const struct odict_entry *oe_id, *oe_sip_callid, *oe_desc;
	struct mbuf *mb = NULL;
	struct session *sess = NULL;
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
	oe_desc = odict_lookup(od, "desc");
	if (!oe_id || !oe_sip_callid || !oe_desc) {
		warning("sync_b2bua: missing json entries\n");
		err = EINVAL;
		goto out;
	}

	debug("sync_b2bua: nosip_call_connect:  id='%s', sip_callid:'%s'\n",
	      oe_id ? oe_id->u.str : "", oe_sip_callid ? oe_sip_callid->u.str : "");

	// check that nosip call exist for the given id.
	sess = get_session_by_nosip_callid(oe_id->u.str);
	if (!sess) {
		warning("sync_b2bua: no session exists for the given nosip call id: %s\n",
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

	// copy the SDP string into a memory buffer.
	mb = mbuf_alloc(str_len(oe_desc->u.str));
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_write_str(mb, oe_desc->u.str);
	if (err) {
		goto out;
	}

	// accept the call with the remote rtp parameters.
	nosip_call_accept(sess->nosip_call, mb, false);
	if (err) {
		warning("sync_b2bua: nosip_call_accept failed (%m)\n", err);
		goto out;
	}

	re_snprintf(a, sizeof(a), "A-%x", sess);
	re_snprintf(b, sizeof(b), "B-%x", sess);

	/* connect the audio/video-bridge devices */
	audio_set_devicename(call_audio(sess->sip_call), a, b);
	audio_set_devicename(nosip_call_audio(sess->nosip_call), b, a);

	// TMP
	nosip_call_sdp_media_debug(sess->nosip_call);

 out:
	if (err)
		mem_deref(sess);

	mem_deref(od);
	mem_deref(mb);

	return err;
}

/**
 * Create a nosip call state object
 *
 * @param pf        Response message print handler
 * @param arg       JSON
 *
 * @param [id]         (char*) ID for the nosip call to be created.
 * @param [sip_callid] (char*) callid of the SIP call related to this object.
 *
 * @return 0 if success, otherwise errorcode
 */
static int nosip_call_create(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *param = carg->prm;
	struct odict *od = NULL;
	struct odict *od_resp = NULL;
	const struct odict_entry *oe_id, *oe_sip_callid;
	struct mbuf *mb = NULL;
	char *sdp = NULL;
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

	debug("sync_b2bua: nosip_call_create: id='%s', sip_callid:'%s'\n",
			oe_id ? oe_id->u.str : "",
			oe_sip_callid ? oe_sip_callid->u.str : "");

	// check that no nosip call exists for the given id.
	sess = get_session_by_nosip_callid(oe_id->u.str);
	if (sess) {
		warning("sync_b2bua: session exists for the given nosip callid: %s\n",
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

	// create a nosip call.
	err = nosip_call_alloc(&sess->nosip_call, oe_id->u.str, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		goto out;
	}

	// prepare response.
	err = odict_alloc(&od_resp, 1);
	if (err)
		goto out;

	err |= nosip_call_sdp_get(sess->nosip_call, &mb, true /* offer */);
	if (err) {
		warning("sync_b2bua: failed to get SDP (%m)\n", err);
		goto out;
	}

	err |= mbuf_strdup(mb, &sdp, mb->end);
	err |= odict_entry_add(od_resp, "desc", ODICT_STRING, sdp);

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

	mem_deref(mb);
	mem_deref(sdp);

	return err;
}

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	int err;
	(void)prm;
	(void)arg;

	if (ev == UA_EVENT_CALL_INCOMING)
	{
		debug("sync_b2bua: CALL_INCOMING: peer=%s	-->	local=%s. id=%s\n",
				call_peeruri(call), call_localuri(call), call_id(call));

		err = new_session(call);
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}

		return;
	}

	if (call)
	{
		struct session *sess;

		sess = get_session_by_sip_callid(call_id(call));
		if (!sess) {
			warning("sync_b2bua: no session found for the given callid: %s\n",
					call_id(call));
			return;
		}

		switch (ev) {
			case CALL_EVENT_ESTABLISHED:
				debug("sync_b2bua: CALL_ESTABLISHED: peer_uri=%s\n",
						call_peeruri(call));
				break;

			case CALL_EVENT_CLOSED:
				debug("sync_b2bua: CALL_CLOSED: %s\n", prm);

				mem_deref(sess);
				break;

			default:
				debug("sync_b2bua: event: %d\n", ev);
		}
	}
}


static int sync_b2bua_status(struct re_printf *pf, void *arg)
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

		err |= re_hprintf(pf, "noSIP call  \n");
		err |= re_hprintf(pf, "%H\n", audio_debug, nosip_call_audio(sess->nosip_call));
	}

	return err;
}

static int rtp_capabilities(struct re_printf *pf, void *arg)
{
	struct nosip_call *call;
	struct mbuf *mb;
	int err;

	(void)arg;

	err = nosip_call_alloc(&call, "capabilities", true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		return err;
	}


	err = nosip_call_sdp_get(call, &mb, true /* offer */);
	if (err) {
		warning("sync_b2bua: failed to get SDP (%m)\n", err);
		goto out;
	}

	err |= re_hprintf(pf, "%b", mb->buf, mb->end);

 out:
	mem_deref(call);
	mem_deref(mb);

	return err;
}

static const struct cmd cmdv[] = {
	{"sync_b2bua_status" , 0, 0      , "sync_b2bua_status" , sync_b2bua_status },
	{"nosip_call_create"  , 0, CMD_PRM, "nosip_call_create"  , nosip_call_create  },
	{"nosip_call_connect" , 0, CMD_PRM, "nosip_call_connect" , nosip_call_connect },
	{"nosip_rtp_capabilities" , 0, 0, "nosip_rtp_capabilities" , rtp_capabilities },
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
