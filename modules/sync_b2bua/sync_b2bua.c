/**
 * @file sync_b2bua/sync_b2bua.c Back-to-Back User-Agent (B2BUA) module
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
 */


/**
 *
 \verbatim

 SIP Audio -> nosip Audio pipeline (aubridge audio driver):

 .       .--------.   .----------.   .-------.
 |       |        |   |          |   |       |
 | RTP -->| auplay |-->| aubridge |-->| ausrc |---> RTP
 |       |        |   |          |   |       |
 '       '--------'   '----------'   '-------'


 nosip Audio -> SIP Audio pipeline (aumix audio driver):

 .       .--------.   .-------.   .-------.
 |       |        |   |       |   |       |
 | RTP -->| auplay |-->| aumix |-->| ausrc |---> RTP
 |       |        |   |       |   |       |
 '       '--------'   '-------'   '-------'

 \endverbatim
 *
 */


struct session {
	struct le le;
	struct le leh_sip;
	struct le leh_nosip;
	struct play *play;              /** Play instance for audio files */
	struct call *sip_call;          /** SIP call instance */
	struct nosip_call *nosip_call;  /** nosip call instance */
	bool connected;
};


static struct list sessionl;
static struct ua *sip_ua;

/* Hash table indexing sessions by SIP callid */
static struct hash *ht_session_by_sip_callid;
/* Hash table indexing sessions by nosip callid */
static struct hash *ht_session_by_nosip_callid;

static struct ausrc *ausrc;
static struct auplay *auplay;

struct hash *sync_ht_device;

/* Audio mixer */
static struct aumix *mixer;
static struct list mixer_sourcel;

/* Hash table indexing mixer_sources by id (nosip callid) */
static struct hash *ht_mixer_source;

static void session_destructor(void *arg)
{
	struct session *sess = arg;

	debug("sync_b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->sip_call, sess->nosip_call);

	list_unlink(&sess->le);
	hash_unlink(&sess->leh_sip);
	hash_unlink(&sess->leh_nosip);

	mem_deref(sess->play);
	mem_deref(sess->sip_call);
	mem_deref(sess->nosip_call);
}


/* ht_session_by_sip_callid lookup helpers */

static bool list_apply_session_by_sip_callid_handler(struct le *le, void *arg)
{
	struct session *st = le->data;

	return (st->sip_call
			&& arg
			&& !str_cmp(call_id(st->sip_call), arg));
}


static struct session *get_session_by_sip_callid(const char* id)
{
	return list_ledata(hash_lookup(ht_session_by_sip_callid,
				    hash_joaat_str(id),
				    list_apply_session_by_sip_callid_handler,
				    (void *)id));
}


/* ht_session_by_nosip_callid lookup helpers */

static bool list_apply_session_by_nosip_callid_handler(
		struct le *le, void *arg)
{
	struct session *st = le->data;

	return (st->nosip_call
			&& arg
			&& !str_cmp(sync_nosip_call_id(st->nosip_call), arg));
}


static struct session *get_session_by_nosip_callid(const char *id)
{
	return list_ledata(hash_lookup(ht_session_by_nosip_callid,
				    hash_joaat_str(id),
				    list_apply_session_by_nosip_callid_handler,
				    (void *)id));
}


/* ht_mixer_source lookup helpers */

static bool list_apply_mixer_source_handler(struct le *le, void *arg)
{
	struct mixer_source *st = le->data;

	return (st->nosip_call
			&& arg
			&& !str_cmp(sync_nosip_call_id(st->nosip_call), arg));
}


static struct mixer_source *get_mixer_source_by_id(const char* id)
{
	return list_ledata(hash_lookup(ht_mixer_source, hash_joaat_str(id),
				    list_apply_mixer_source_handler,
				    (void *)id));
}


static int new_session(struct call *call)
{
	struct session *sess;
	int err;

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	sess->sip_call = mem_ref(call);
	sess->connected = false;

	err = ua_answer(call_get_ua(call), call);
	if (err) {
		warning("sync_b2bua: ua_answer failed (%m)\n", err);
		return err;
	}

	list_append(&sessionl, &sess->le, sess);

	/* Index the session by SIP callid */
	hash_append(ht_session_by_sip_callid, hash_joaat_str(call_id(call)),
			        &sess->leh_sip, sess);

	return err;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			 struct call *call, const char *prm, void *arg)
{
	int err;

	(void)prm;
	(void)arg;

	if (ev == UA_EVENT_CALL_INCOMING) {
		debug("sync_b2bua: CALL_INCOMING:"
			" peer=%s --> local=%s. id=%s\n",
			call_peeruri(call),
			call_localuri(call),
			call_id(call));

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
			warning("sync_b2bua: no session found for"
				" the given callid: %s\n",
				call_id(call));
			return;
		}

		switch (ev) {
			case UA_EVENT_CALL_ESTABLISHED:
				debug("sync_b2bua: CALL_ESTABLISHED:"
					" peer_uri=%s\n",
					call_peeruri(call));
				break;

			case UA_EVENT_CALL_CLOSED:
				debug("sync_b2bua: CALL_CLOSED: %s\n", prm);

				mem_deref(sess);
				break;

			default:
				break;
		}
	}
}


/* Get the current number of sessions */
size_t sync_session_count(void)
{
	return list_count(&sessionl);
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
int sync_nosip_call_create(struct mbuf **mb, const char *id,
		   const char *sip_callid)
{
	struct session *sess;
	int err;

	/* Check that no nosip call exists for the given id */
	sess = get_session_by_nosip_callid(id);
	if (sess) {
		warning("sync_b2bua: session found for"
			" the given nosip callid: %s\n",
			id);
		err = EINVAL;
		goto out;
	}

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given SIP callid: %s\n",
			sip_callid);
		err = EINVAL;
		goto out;
	}

	/* Create nosip call */
	err = sync_nosip_call_alloc(&sess->nosip_call, id, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		goto out;
	}

	/* Index the session by nosip callid */
	hash_append(ht_session_by_nosip_callid, hash_joaat_str(id),
			        &sess->leh_nosip, sess);

	err |= sync_nosip_call_sdp_get(sess->nosip_call, mb, true /* offer */);
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
int sync_nosip_call_connect(const char *id, const char *sip_callid,
		   struct mbuf *mb)
{
	struct session *sess;
	char device[64];
	int err;

	/* Check that nosip call exist for the given id */
	sess = get_session_by_nosip_callid(id);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given nosip call id: %s\n",
			id);
		err = EINVAL;
		goto out;
	}

	/* Check that SIP call exist for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given callid: %s\n",
			sip_callid);
		err = EINVAL;
		goto out;
	}

	if (sess->connected) {
		warning("sync_b2bua: nosip_call already connected: %s\n",
			id);
		err = EINVAL;
		goto out;

	}

	/* Stop any ongoing play file */
	sess->play = mem_deref(sess->play);

	/* Accept the call with the remote SDP */
	err = sync_nosip_call_accept(sess->nosip_call, mb, false);
	if (err) {
		warning("sync_b2bua: nosip_call_accept failed (%m)\n", err);
		goto out;
	}

	sess->connected = true;

	/**
	 * The audio coming from SIP call is the source of nosip call:
	 * (audio player SIP call -> audio source nosip call)
	 */
	(void)re_snprintf(device, sizeof(device),
			  "sip_to_nosip-%x",
			  call_id(sess->sip_call));

	/* Set SIP call audio player to nosip call audio source */
	err = audio_set_player(call_audio(sess->sip_call), "aubridge", device);
	err |= audio_set_source(sync_nosip_call_audio(sess->nosip_call),
				 "aubridge", device);
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
 * Terminate a SIP call state object
 *
 * @param sip_callid ID of the SIP call to be connected to
 * @param reason     Hangup reason
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_sip_call_hangup(const char *sip_callid, const char *reason)
{
	struct session *sess;

	/* Check that SIP call exist for the given id */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given SIP call id: %s\n",
			sip_callid);
		return EINVAL;
	}

	/* Hangup the call */
	ua_hangup(sip_ua, sess->sip_call, 0 /* code */, reason);

	return 0;
}


int sync_status(struct re_printf *pf)
{
	struct le *le;
	int i;
	int err = 0;

	err |= re_hprintf(pf, "Sessions: (%zu)\n\n", list_count(&sessionl));

	for (le = sessionl.head, i=1; le; le = le->next, i++) {
		struct session *sess = le->data;

		err |= re_hprintf(pf,
				   "----------- %d (%s)-----------\n\n",
				i, call_peeruri(sess->sip_call));
		err |= re_hprintf(pf, "SIP call:\n\n");
		err |= re_hprintf(pf, "%H\n", call_status, sess->sip_call);
		err |= re_hprintf(pf, "%H\n", audio_debug,
				   call_audio(sess->sip_call));

		err |= re_hprintf(pf, "nosip call:\n");
		err |= re_hprintf(pf, "%H\n", audio_debug,
				   sync_nosip_call_audio(sess->nosip_call));

		if (err)
			goto out;
	}

	err |= re_hprintf(pf, "Mixer: (%zup)\n\n", aumix_source_count(mixer));

	for (le = mixer_sourcel.head, i=1; le; le = le->next, i++) {
		struct mixer_source *src = le->data;

		err |= re_hprintf(pf,
				   "----------- %d -----------\n", i);
		err |= re_hprintf(pf, "%H\n", audio_debug,
				   sync_nosip_call_audio(src->nosip_call));

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
int sync_play_start(const char *sip_callid, const char *file, bool loop)
{
	static const char module[9] = "aubridge";

	struct config *cfg = conf_config();
	struct player *player = baresip_player();
	struct session *sess;
	char device[64];
	int err;

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given SIP callid: %s\n",
			sip_callid);
		err = EINVAL;
		goto out;
	}

	if (!cfg){
		err = ENOENT;
		goto out;
	}


	/* Stop any file playback on this session */
	sess->play = mem_deref(sess->play);

	/**
	 * 'playfile' creates an 'auplay' state considering the audio alert
	 *  module and device from the config.
	 *
	 * see 'src/play.c'
	 */

	(void)re_snprintf(device, sizeof(cfg->audio.alert_dev),
			  "play_%x", sess);

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
int sync_play_stop(const char *sip_callid)
{
	struct session *sess;

	/* Check that a SIP call exists for the given SIP callid */
	sess = get_session_by_sip_callid(sip_callid);
	if (!sess) {
		warning("sync_b2bua: no session found for"
			" the given SIP callid: %s\n",
			sip_callid);
		return EINVAL;
	}

	sess->play = mem_deref(sess->play);

	return 0;
}


/**
 * Get a list of SIP callids that that are currently playing a file
 *
 * @param od_array  odict instance
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_play_list(struct odict *od_array)
{
	struct le *le;
	int err;

	for (le = list_head((&sessionl)); (le); (le) = (le)->next) {
		struct session *sess = le->data;

		if (!sess->play)
			continue;

		err = odict_entry_add(od_array, "",
				  ODICT_STRING, call_id(sess->sip_call));
		if (err)
			return err;
	}

	return 0;
}


/**
 * Get the RTP capabilities of the baresip instance
 *
 * @param pf Print handler for debug output
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_rtp_capabilities(struct re_printf *pf)
{
	struct nosip_call *call;
	struct mbuf *mb;
	int err;

	err = sync_nosip_call_alloc(&call, "capabilities", true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		return err;
	}

	err = sync_nosip_call_sdp_get(call, &mb, true /* offer */);
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
int sync_mixer_source_add(struct mbuf **answer, const char *id,
		   const char *sip_callid, struct mbuf *offer)
{
	struct session *sess = NULL;
	struct nosip_call *nosip_call = NULL;
	struct mixer_source *mixer_source = NULL;
	int err;

	/* Check that a mixer source does not exist for the given id */
	if (get_mixer_source_by_id(id)) {
		warning("sync_b2bua: mixer source found for"
			" the given id: %s\n",
			id);
		return EINVAL;
	}
	/* Create a nosip call */
	err = sync_nosip_call_alloc(&nosip_call, id, false /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_alloc failed (%m)\n", err);
		goto out;
	}

	/* Accept the call with the remote SDP */
	err = sync_nosip_call_accept(nosip_call, offer, true /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_accept failed (%m)\n", err);
		goto out;
	}

	/* Retrieve SDP answer */
	err = sync_nosip_call_sdp_get(nosip_call, answer, false /* offer */);
	if (err) {
		warning("sync_b2bua: nosip_call_sdp_get failed (%m)\n", err);
		goto out;
	}

	/* Create a mixer source */
	if (!sip_callid)
	{
		err = sync_mixer_source_alloc(&mixer_source, mixer, id,
				nosip_call, false);
		if (err) {
			warning("sync_b2bua: mixer_source_alloc failed (%m)\n",
				err);
			goto out;
		}
	}
	else
	{
		/* Check that SIP call exist for the given SIP callid */
		sess = get_session_by_sip_callid(sip_callid);
		if (!sess) {
			warning("sync_b2bua: no session found for"
				" the given SIP callid: %s\n",
				sip_callid);
			err = EINVAL;
			goto out;
		}

		err = sync_mixer_source_alloc(&mixer_source, mixer, id,
				nosip_call, true);
		if (err) {
			warning("sync_b2bua: mixer_source_alloc failed (%m)\n",
					err);
			goto out;
		}

		/* Reset the 'ausrc' device name of the sip call audio */
		audio_set_devicename(call_audio(sess->sip_call), id, "");

		/* Set audio source to the just allocated one */
		err = audio_set_source(call_audio(sess->sip_call),
				  "aumix", id);
		if (err) {
			warning("mixer_source: audio_set_source failed (%m)\n",
					err);
			goto out;
		}
	}

	list_append(&mixer_sourcel, &mixer_source->le, mixer_source);

	/* Index the mixer source by id */
	hash_append(ht_mixer_source, hash_joaat_str(id),
			        &mixer_source->leh, mixer_source);

	/* Set the audio play device name */
	audio_set_devicename(sync_nosip_call_audio(nosip_call), "", id);

	/* Set audio player to the just allocated one */
	err = audio_set_player(sync_nosip_call_audio(nosip_call), "aumix", id);
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
int sync_mixer_source_del(const char *id)
{
	struct mixer_source *src;

	/* Check that a mixer source exists for the given id */
	src = get_mixer_source_by_id(id);
	if (!src) {
		warning("sync_b2bua: no mixer source found for"
				" the given id: %s\n",
				id);
		return EINVAL;
	}

	mem_deref(src);

	return 0;
}


/**
 * Enable a mixer source.
 *
 * @param id           ID for the mixer source to be deleted
 * @param [sip_callid] ID of the SIP call source of the audio
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_mixer_source_enable(const char *id, const char *sip_callid)
{
	struct mixer_source *src;
	struct session *sess;
	int err = 0;

	/* Check that a mixer source exists for the given id */
	src = get_mixer_source_by_id(id);
	if (!src) {
		warning("sync_b2bua: no mixer source found for"
				" the given id: %s\n",
				id);
		return EINVAL;
	}

	if (sip_callid) {
		/* Check that SIP call exist for the given SIP callid */
		sess = get_session_by_sip_callid(sip_callid);
		if (!sess) {
			warning("sync_b2bua: no session found for"
					" the given SIP callid: %s\n",
					sip_callid);
			err = EINVAL;
			goto out;
		}

		/* Reset the 'ausrc' device name of the sip call audio */
		audio_set_devicename(call_audio(sess->sip_call), id, "");

		/*
		 * Set audio source accordingly
		 * By setting the corresponding aumix audio source,
		 * the aumix_source will be enabled.
		 */
		err = audio_set_source(call_audio(sess->sip_call),
				  "aumix", id);
		if (err) {
			warning("mixer_source: audio_set_source failed (%m)\n",
					err);
			goto out;
		}
	}
	else {
		sync_device_enable(src->dev);
	}

 out:
	return err;
}

/**
 * Disable a mixer source.
 *
 * @param id ID for the mixer source to be deleted
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_mixer_source_disable(const char *id)
{
	struct mixer_source *src;
	struct aumix_source *aumix_src;

	/* Check that a mixer source exists for the given id */
	src = get_mixer_source_by_id(id);
	if (!src) {
		warning("sync_b2bua: no mixer source found for"
				" the given id: %s\n",
				id);
		return EINVAL;
	}

	aumix_src = sync_device_aumix_src(src->dev);
	if (!aumix_src)
		goto out;

	aumix_source_enable(aumix_src, false);

 out:
	return 0;
}

/**
 * Play an audio file into the mixer.
 *
 * @param file  Name of the file to be played
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_mixer_play(const char *file)
{
	struct config *cfg = conf_config();
	char filepath[256];
	int err;

	(void)re_snprintf(filepath, sizeof(filepath), "%s/%s",
			  cfg->audio.audio_path, file);

	err = aumix_playfile(mixer, filepath);
	if (err)
		warning("sync_b2bua: mixer_play failed (%m)\n", err);

	return err;
}


static int module_init(void)
{
	struct config *cfg = conf_config();
	uint32_t srate = cfg->audio.srate_play;
	int err;

	sip_ua = uag_find_param("b2bua", "inbound");

	if (!sip_ua) {
		warning("sync_b2bua: inbound UA not found\n");
		return ENOENT;
	}

	/* Allocate device hash table */
	err = hash_alloc(&sync_ht_device, 256);
	if (err)
		return err;

	/*
	 * Allocate session hash table
	 * (sessions indexed by SIP callid and nosip callid)
	 */
	err = hash_alloc(&ht_session_by_sip_callid, 256);
	err |= hash_alloc(&ht_session_by_nosip_callid, 256);
	if (err)
		return err;

	/* Allocate mixer_source hash table */
	err = hash_alloc(&ht_mixer_source, 256);
	if (err)
		return err;

	err = cmd_register(baresip_commands(), cmdv, command_count);
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	/* The inbound UA will handle all non-matching requests */
	ua_set_catchall(sip_ua, true);

	/* Register the mixer source and player */
	err = ausrc_register(&ausrc, baresip_ausrcl(), "aumix",
			  sync_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			  "aumix", sync_play_alloc);
	if (err) {
		return err;
	}

	/* Start audio mixer */
	err = aumix_alloc(&mixer, srate ? srate : 48000,
			  1 /* channels */, 20 /* ptime */);
	if (err) {
		warning("aumix\n");
		return err;
	}

	debug("sync_b2bua: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("sync_b2bua: module closing..\n");

	mem_deref(auplay);
	mem_deref(ausrc);

	sync_ht_device = mem_deref(sync_ht_device);

	hash_clear(ht_session_by_sip_callid);
	ht_session_by_sip_callid = mem_deref(ht_session_by_sip_callid);

	hash_clear(ht_session_by_nosip_callid);
	ht_session_by_nosip_callid = mem_deref(ht_session_by_nosip_callid);

	hash_clear(ht_mixer_source);
	ht_mixer_source = mem_deref(ht_mixer_source);

	info("sync_b2bua: flushing %u sessions\n", list_count(&sessionl));
	list_flush(&sessionl);

	info("sync_b2bua: flushing %u mixer sources\n",
			list_count(&mixer_sourcel));
	list_flush(&mixer_sourcel);

	mem_deref(mixer);

	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(sync_b2bua) = {
	"sync_b2bua",
	"application",
	module_init,
	module_close
};
