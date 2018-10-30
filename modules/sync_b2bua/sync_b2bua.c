/**
 * @file sync_b2bua.c Back-to-Back User-Agent (B2BUA) module
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "sync_b2bua.h"
#include "aumix.h"

/**
 * @defgroup sync_b2bua sync_b2bua
 *
 * Sync Back-to-Back User-Agent (B2BUA) module
 *
 * N session objects
 * 1 session object has 2 call objects (SIP call, noSIP call)
 */

const int MAX_SESSIONS = 100;

struct session {
	struct le le;
	struct play *play;              /** Play instance for audio files */
	struct call *sip_call;          /** SIP call instance */
	struct nosip_call *nosip_call;  /** nosip call instance */
};

static struct list sessionl;
static struct ua *sip_ua;

static struct ausrc *ausrc;
static struct auplay *auplay;

/* Audio mixer */
static struct aumix *mixer;
static struct list mixer_sourcel;


static void session_destructor(void *arg)
{
	struct session *sess = arg;

	debug("sync_b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->sip_call, sess->nosip_call);

	list_unlink(&sess->le);

	mem_deref(sess->play);
	mem_deref(sess->sip_call);
	mem_deref(sess->nosip_call);
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


static struct session *get_session_by_nosip_callid(const char* id)
{
	struct le *le;

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (sess->nosip_call && !strcmp(nosip_call_id(sess->nosip_call), id))
			return sess;
	}

	return NULL;
}


static struct mixer_source *get_mixer_source_by_id(const char* id)
{
	struct le *le;

	for ((le) = list_head((&mixer_sourcel)); (le); (le) = (le)->next) {
		struct mixer_source *src = le->data;

		/* This should never happen */
		if (!src->nosip_call)
			continue;

		if (!strcmp(nosip_call_id(src->nosip_call), id))
			return src;
	}

	return NULL;
}


static int new_session(struct call *call)
{
	struct session *sess;

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	mem_ref(call);

	sess->sip_call = call;

	ua_answer(call_get_ua(call), call);

	list_append(&sessionl, &sess->le, sess);

	return 0;
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

	else if (call)
	{
		struct session *sess;

		sess = get_session_by_sip_callid(call_id(call));
		if (!sess) {
			warning("sync_b2bua: no session found for the given callid: %s\n",
					call_id(call));
			return;
		}

		switch (ev) {
			case UA_EVENT_CALL_ESTABLISHED:
				debug("sync_b2bua: CALL_ESTABLISHED: peer_uri=%s\n",
						call_peeruri(call));
				break;

			case UA_EVENT_CALL_CLOSED:
				debug("sync_b2bua: CALL_CLOSED: %s\n", prm);

				mem_deref(sess);
				break;

			default:
				debug("sync_b2bua: event: %d\n", ev);
		}
	}
}


/**
 * Create a nosip call state object
 *
 * @param mb         Pointer to the SDP offer memory buffer
 * @param id         ID for the nosip call to be created
 * @param sip_callid ID of the SIP call to be connected to
 *
 * @return 0 if success, otherwise errorcode
 */
int nosip_call_create(struct mbuf **mb, const char *id,
		   const char *sip_callid)
{
	struct session *sess;
	int err;

	/* Check that no nosip call exists for the given id */
	sess = get_session_by_nosip_callid(id);
	if (sess) {
		warning("sync_b2bua: session found for the given nosip callid: %s\n",
			id);
		err = ENOENT;
		goto out;
	}

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for the given SIP callid: %s\n",
				sip_callid);
		err = ENOENT;
		goto out;
	}

	/* Create nosip call */
	err = nosip_call_alloc(&sess->nosip_call, id, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		goto out;
	}

	err |= nosip_call_sdp_get(sess->nosip_call, mb, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_sdp_get failed (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(sess);

	return err;
}


/**
 * Connect a nosip call with the corresponding SIP call
 *
 * @param id         ID for the nosip call to be created
 * @param sip_callid ID of the SIP call to be connected to
 * @param mb         SDP answer
 *
 * @return 0 if success, otherwise errorcode
 */
int nosip_call_connect(const char *id, const char *sip_callid,
		   struct mbuf *mb)
{
	struct session *sess;
	char a[64], b[64];
	int err;

	/* Check that nosip call exist for the given id */
	sess = get_session_by_nosip_callid(id);
	if (!sess) {
		warning("sync_b2bua: no session found for the given nosip call id: %s\n",
				id);
		err = ENOENT;
		goto out;
	}

	/* Check that SIP call exist for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for the given callid: %s\n",
				sip_callid);
		err = ENOENT;
		goto out;
	}

	/* Stop any ongoing play file */
	sess->play = mem_deref(sess->play);

	/* Accept the call with the remote SDP */
	err = nosip_call_accept(sess->nosip_call, mb, false);
	if (err) {
		warning("sync_b2bua: nosip_call_accept failed (%m)\n", err);
		goto out;
	}

	/**
	 * audio player for SIP call: b
	 * audio source for nosip call: b
	 *
	 * RTP coming from SIP call, is the souce of the nosip call
	 * (audio player SIP call -> audio source nosip call)
	 */
	re_snprintf(a, sizeof(a), "mixer_src-%x", call_id(sess->sip_call));
	re_snprintf(b, sizeof(b), "sip_to_nosip-%x", call_id(sess->sip_call));

	/* Connect the audio/video-bridge devices */
	/* audio_set_devicename(call_audio(sess->sip_call), a, b); */
	/* audio_set_devicename(nosip_call_audio(sess->nosip_call), b, a); */

	/* Set SIP call audio player to noSIP call audio source */
	err = audio_set_player(call_audio(sess->sip_call), "aubridge", b);
	err |= audio_set_source(nosip_call_audio(sess->nosip_call), "aubridge", b);
	if (err) {
		warning("sync_b2bua: audio_set_player failed (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(sess);

	return err;
}


/**
 * Terminate a nosip call state object
 *
 * @param pf         Print handler for debug output
 * @param sip_callid ID of the SIP call to be connected to
 *
 * @return 0 if success, otherwise errorcode
 */
int sip_call_hangup(const char *sip_callid, const char *reason)
{
	struct session *sess;

	/* Check that SIP call exist for the given id */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for the given SIP call id: %s\n",
				sip_callid);
		return ENOENT;
	}

	/* Hangup the call */
	ua_hangup(sip_ua, sess->sip_call, 0 /* code */, reason);

	return 0;
}


int status(struct re_printf *pf)
{
	struct le *le;
	int i;
	int err = 0;

	err |= re_hprintf(pf, "Sessions: (%zu)\n\n", list_count(&sessionl));

	for (le = sessionl.head, i=1; le; le = le->next, i++) {
		struct session *sess = le->data;

		err |= re_hprintf(pf, "----------------- %d (%s)-----------------\n\n",
				i, call_peeruri(sess->sip_call));
		err |= re_hprintf(pf, "SIP call:\n\n");
		err |= re_hprintf(pf, "%H\n", call_status, sess->sip_call);
		err |= re_hprintf(pf, "%H\n", audio_debug, call_audio(sess->sip_call));

		err |= re_hprintf(pf, "noSIP call:\n");
		err |= re_hprintf(pf, "%H\n", audio_debug, nosip_call_audio(sess->nosip_call));

		if (err)
			goto out;
	}

	err |= re_hprintf(pf, "Mixer: (%zup)\n\n", aumix_source_count(mixer));

	for (le = mixer_sourcel.head, i=1; le; le = le->next, i++) {
		struct mixer_source *src = le->data;

		err |= re_hprintf(pf, "----------------- %d -----------------\n", i);
		err |= re_hprintf(pf, "%H\n", audio_debug, nosip_call_audio(src->nosip_call));

		if (err)
			goto out;
	}

 out:
	return err;
}


/**
 * Start playing a file on a SIP call
 *
 * @param sip_callid ID of the SIP call
 * @param file       Name of the file to be played
 * @param loop       True if the file is to be played on loop
 *
 * @return 0 if success, otherwise errorcode
 */
int play_start(const char *sip_callid, const char *file, bool loop)
{
	static const char module[9] = "aubridge";

	struct player *player = baresip_player();
	struct session *sess;
	char device[64];
	int err;

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for the given SIP callid: %s\n",
				sip_callid);
		err = ENOENT;
		goto out;
	}

	struct config *cfg = conf_config();
	if (!cfg){
		err = ENOENT;
		goto out;
	}

	/**
	 * 'playfile' creates an 'auplay' state considering the audio alert
	 *  module and device from the config.
	 *
	 * see 'src/play.c'
	 */

	re_snprintf(device, sizeof(cfg->audio.alert_dev), "play_%x", sess);

	/* Update the audo alert module and device in the config */
	str_ncpy(cfg->audio.alert_mod, module, sizeof(module));
	str_ncpy(cfg->audio.alert_dev, device, sizeof(device));

	debug("audio alert settings modified. alert_mod:%s, alert_dev:%s\n",
			cfg->audio.alert_mod, cfg->audio.alert_dev);

	/* Reset the 'ausrc' device name of the sip call audio */
	audio_set_devicename(call_audio(sess->sip_call), device, "");

	/* Set SIP call audio source to the session's play audio play */
	err = audio_set_source(call_audio(sess->sip_call), "aubridge", device);
	if (err) {
		warning("sync_b2bua: audio_set_source failed: (%m)\n", err);
		goto out;
	}

	/* Stop any file playback */
	sess->play = mem_deref(sess->play);

	err |= play_file(&sess->play, player, file, loop ? -1: 1);
	if (err)
		goto out;

 out:

	return err;
}


/**
 * Stop playing a file on a SIP call
 *
 * @param sip_callid ID of the SIP call
 *
 * @return 0 if success, otherwise errorcode
 */
int play_stop(const char *sip_callid)
{
	struct session *sess;

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for the given SIP callid: %s\n",
				sip_callid);
		return ENOENT;
	}

	sess->play = mem_deref(sess->play);

	return 0;
}


/**
 * Get a list of SIP callids that that are currently playing a file
 *
 * @param pf  Print handler for debug output
 *
 * @return 0 if success, otherwise errorcode
 */
int play_list(struct odict *od_array)
{
	struct le *le;
	int err;

	for ((le) = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (!sess->play)
			continue;

		err = odict_entry_add(od_array, "", ODICT_STRING, call_id(sess->sip_call));
		if (err)
			return err;
	}

	return 0;
}


/**
 * Get the RTP capabilities of the baresip instance
 *
 * @param pf  Print handler for debug output
 *
 * @return 0 if success, otherwise errorcode
 */
int rtp_capabilities(struct re_printf *pf)
{
	struct nosip_call *call;
	struct mbuf *mb;
	int err;

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


/**
 * Add a source into the mixer.
 *
 * @param answer       Pointer to the SDP answer mbuf
 * @param id           ID for the nosip call to be created
 * @param [sip_callid] ID of the SIP call to be connected to
 * @param offer        SDP offer mbuf
 *
 * @return 0 if success, otherwise errorcode
 */
int mixer_source_add(struct mbuf **answer, const char *id,
		   const char *sip_callid, struct mbuf *offer)
{
	struct session *sess = NULL;
	struct nosip_call *nosip_call = NULL;
	struct mixer_source *mixer_source = NULL;
	int err;

	/* Check that a mixer source does not exist for the given id */
	if (get_mixer_source_by_id(id)) {
		warning("sync_b2bua: mixer source found for the given id: %s\n",
				id);
		return ENOENT;
	}
	/* Create a nosip call */
	err = nosip_call_alloc(&nosip_call, id, false /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		goto out;
	}

	/* Accept the call with the remote SDP */
	err = nosip_call_accept(nosip_call, offer, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_accept failed (%m)\n", err);
		goto out;
	}

	/* Retrieve SDP answer */
	err = nosip_call_sdp_get(nosip_call, answer, false /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_sdp_get failed (%m)\n", err);
		goto out;
	}

	/* Create a mixer source */
	/*
	 * NOTE: Why not create the nosip_call inside mixer_source_alloc?
	 * nosip_call instance is a member of the mixer_source and it dereferences
	 * the nosip_call on destruction
	 * */
	if (!sip_callid)
	{
		err = mixer_source_alloc(&mixer_source, mixer, id,
				nosip_call, NULL);
		if (err) {
			warning("sync_b2bua: mixer_source_alloc failed (%m)\n", err);
			goto out;
		}
	}
	else
	{
		/* Check that SIP call exist for the given SIP callid */
		sess = get_session_by_sip_callid(sip_callid);
		if (!sess) {
			warning("sync_b2bua: no session found for the given SIP callid: %s\n",
					sip_callid);
			err = EINVAL;
			goto out;
		}

		err = mixer_source_alloc(&mixer_source, mixer, id,
				nosip_call, call_audio(sess->sip_call));
		if (err) {
			warning("sync_b2bua: mixer_source_alloc failed (%m)\n", err);
			goto out;
		}

		/* Reset the 'ausrc' device name of the sip call audio */
		audio_set_devicename(call_audio(sess->sip_call), id, "");

		/* Set audio source to the just allocated one */
		err = audio_set_source(call_audio(sess->sip_call), "aumix", id);
		if (err) {
			warning("mixer_source: audio_set_source failed (%m)\n", err);
			goto out;
		}
	}

	list_append(&mixer_sourcel, &mixer_source->le, mixer_source);

	/* Set the audio play device name */
	audio_set_devicename(nosip_call_audio(nosip_call), "", id);

	/* Set audio player to the just allocated one */
	err = audio_set_player(nosip_call_audio(nosip_call), "aumix", id);
	if (err) {
		warning("mixer_source: audio_set_player failed (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(nosip_call);

	return err;
}


/**
 * Delete a source from the mixer.
 *
 * @param id ID for the mixer source to be deleted
 *
 * @return 0 if success, otherwise errorcode
 */
int mixer_source_del(const char *id)
{
	struct mixer_source *src;

	/* Check that a mixer source exists for the given id */
	src = get_mixer_source_by_id(id);
	if (!src) {
		warning("sync_b2bua: no mixer source found for the given id: %s\n",
				id);
		return ENOENT;
	}

	mem_deref(src);

	return 0;
}


static int module_init(void)
{
	int err;

	sip_ua = uag_find_param("b2bua", "inbound");

	if (!sip_ua) {
		warning("sync_b2bua: inbound UA not found\n");
		return ENOENT;
	}

	err = cmd_register(baresip_commands(), cmdv, command_count);
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	/* The inbound UA will handle all non-matching requests */
	ua_set_catchall(sip_ua, true);

	/* Register the mixer source and player */
	err = ausrc_register(&ausrc, baresip_ausrcl(), "aumix", src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "aumix", play_alloc);
	if (err)
		return err;

	/* Start audio mixer */
	err = aumix_alloc(&mixer, 8000 /* srate */, 1 /* channels */, 20 /* ptime */);
	if (err)
		return err;

	debug("sync_b2bua: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("sync_b2bua: module closing..\n");

	mem_deref(auplay);
	mem_deref(ausrc);

	info("sync_b2bua: flushing %u sessions\n", list_count(&sessionl));
	list_flush(&sessionl);

	info("sync_b2bua: flushing %u mixer sources\n", list_count(&mixer_sourcel));
	list_flush(&mixer_sourcel);

	mem_deref(mixer);

	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(b2bua) = {
	"sync_b2bua",
	"application",
	module_init,
	module_close
};
