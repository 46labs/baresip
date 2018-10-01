/**
 * @file b2bua.c Back-to-Back User-Agent (B2BUA) module
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <baresip.h>

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

	debug("b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->sip_call, sess->sfu_call);

	list_unlink(&sess->le);

	if (sess->sfu_call)
		mem_deref(sess->sfu_call);

	if (sess->sip_call)
		mem_deref(sess->sip_call);
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct session *sess = arg;

	switch (ev) {

	case CALL_EVENT_ESTABLISHED:
		debug("b2bua: CALL_ESTABLISHED: peer_uri=%s\n",
		      call_peeruri(call));
		break;

	case CALL_EVENT_CLOSED:
		debug("b2bua: CALL_CLOSED: %s\n", str);

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

	debug("b2bua: received DTMF event: key = '%c'\n", key ? key : '.');
}


static int new_session(struct call *call)
{
	struct session *sess;
	char a[64], b[64];
	int err;

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	sess->sip_call = call;

	ua_answer(call_get_ua(call), call);

	// Create a SFU call.
	err = sfu_call_alloc(&sess->sfu_call, true /* offerer */);

#if 1
	sfu_call_sdp_debug(sess->sfu_call, true /* offerer */);

	struct mbuf *desc;

	// Get the local SDP.
	sfu_call_sdp_get(sess->sfu_call, &desc, true);

	// Accept the call with the same SDP, as it was remote.
	sfu_call_accept(sess->sfu_call, desc, false);
	if (err) {
		warning("b2bua: sfu_call_accept failed (%m)\n", err);
		goto out;
	}

	mem_deref(desc);
#endif

	re_snprintf(a, sizeof(a), "A-%x", sess);
	re_snprintf(b, sizeof(b), "B-%x", sess);

	/* connect the audio/video-bridge devices */
	audio_set_devicename(call_audio(sess->sip_call), a, b);
	audio_set_devicename(sfu_call_audio(sess->sfu_call), b, a);

	call_set_handlers(sess->sip_call, call_event_handler,
			  call_dtmf_handler, sess);

	list_append(&sessionl, &sess->le, sess);

 out:
	if (err)
		mem_deref(sess);

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
		debug("b2bua: CALL_INCOMING: peer=%s  -->  local=%s\n",
		      call_peeruri(call), call_localuri(call));

		err = new_session(call);
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		break;

	default:
		break;
	}
}


static int b2bua_status(struct re_printf *pf, void *arg)
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
	{"b2bua", 0,       0, "b2bua status", b2bua_status },
};


static int module_init(void)
{
	int err;

	ua_in  = uag_find_param("b2bua", "inbound");

	if (!ua_in) {
		warning("b2bua: inbound UA not found\n");
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

	debug("b2bua: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("b2bua: module closing..\n");

	if (!list_isempty(&sessionl)) {

		info("b2bua: flushing %u sessions\n", list_count(&sessionl));
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
