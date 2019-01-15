/**
 * @file sync_b2bua/nosip_call.c nosip call state
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "../src/core.h"
#include "sync_b2bua.h"


struct nosip_call {
	char *id;                /**< nosip call id */
	struct audio *audio;     /**< Audio stream */
	struct sdp_session *sdp; /**< SDP Session  */
};


static void nosip_call_destructor(void *arg)
{
	struct nosip_call *call = arg;

	audio_stop(call->audio);

	mem_deref(call->id);
	mem_deref(call->audio);
	mem_deref(call->sdp);
}


static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (uint8_t *)p, size);
}


int sync_nosip_call_sdp_get(const struct nosip_call *call, struct mbuf **desc,
		   bool offer)
{
	int err;

	if (!call || !desc)
		return EINVAL;

	err = sdp_encode(desc, call->sdp, offer);
	if (err) {
		warning("nosip_call: sdp_encode failed (%m)\n", err);
		goto out;
	}

 out:
	return err;
}


int sync_nosip_call_sdp_debug(const struct nosip_call *call, bool offer)
{
	struct mbuf *desc;
	int err;

	err = sync_nosip_call_sdp_get(call, &desc, offer);
	if (err) {
		goto out;
	}

	info("- - - - - S D P - %s - - - - -\n"
	     "%b"
	     " - - - - - - - - - - - - - - -\n",
	     offer ? "O f f e r" : "A n s w e r", desc->buf, desc->end);

	mem_deref(desc);

 out:
	return err;
}


int sync_nosip_call_sdp_media_debug(const struct nosip_call *call)
{
	struct mbuf *mb = mbuf_alloc(2048);
	struct re_printf pf = {print_handler, mb};
	int err;

	err = sdp_media_debug(&pf, stream_sdpmedia(audio_strm(call->audio)));

	info("- - - - - S D P  M E D I A - - - - -\n"
	    "%b"
	    "- - - - - - - - - - - - - - - - - - -\n",
	    mb->buf, mb->end);

	mem_deref(mb);

	return err;
}


/**
 * Allocate a new nosip Call state object
 *
 * @param callp Pointer to allocated nosip Call state object
 * @param id    Id for the new call
 * @param offer Boolean
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_nosip_call_alloc(struct nosip_call **callp, const char *id,
		bool offer)
{
	const struct network *net = baresip_network();
	const struct config *cfg = conf_config();
	struct nosip_call *call;
	struct stream_param stream_prm;
	struct sa laddr;
	int err;

	debug("nosip_call_alloc\n");

	memset(&stream_prm, 0, sizeof(stream_prm));
	stream_prm.use_rtp = true;
	stream_prm.af      = AF_INET;
	stream_prm.cname   = id;

	call = mem_zalloc(sizeof(*call), nosip_call_destructor);
	if (!call)
		return ENOMEM;

	err = str_dup(&call->id, id);
	if (err)
		return err;

	sa_cpy(&laddr, net_laddr_af(net, net_af(net)));

	/* Init SDP info */
	err = sdp_session_alloc(&call->sdp, &laddr);
	if (err)
		goto out;

	err = audio_alloc(&call->audio, &stream_prm,
			cfg,
			NULL /* call */, call->sdp, 0 /* SDP label */,
			NULL /* mnat */, NULL /* mnat_sess */,
			NULL /* menc */, NULL /* menc_sess */,
			20 /* ptime */, baresip_aucodecl(), offer,
			NULL /* audio_event_h */, NULL /* audio_err_h */,
			call);
	if (err) {
		warning("nosip_call: audio_alloc failed (%m)\n", err);
		goto out;
	}

	*callp = call;

 out:
	if (err)
		mem_deref(call);

	return err;
}


/**
 * Get the audio object for the current call
 *
 * param call nosip Call object
 *
 * @return Audio object
 */
struct audio *sync_nosip_call_audio(const struct nosip_call *call)
{
	return call ? call->audio : NULL;
}


const char *sync_nosip_call_id(const struct nosip_call *call)
{
	return call ? call->id : NULL;
}


/**
 * Accept the call
 *
 * @param call  nosip Call object
 * @param desc  mbuf pointing to the SDP
 * @param offer Flag indicating whether 'desc' is an offer
 *
 * @return 0 if success, otherwise errorcode
 */
int sync_nosip_call_accept(struct nosip_call *call, struct mbuf *desc,
		bool offer)
{
	int err;

	/* reset buffer possition */
	desc->pos = 0;

	err = sdp_decode(call->sdp, desc, offer);
	if (err) {
		warning("b2bua: sdp_decode failed (%m)\n", err);
		goto out;
	}

	sync_nosip_audio_start(call);

 out:
	return err;
}


/**
 * Start audio object
 *
 * @param call nosip Call object
 *
 */
void sync_nosip_audio_start(const struct nosip_call *call)
{
	const struct sdp_media *m;
	const struct sdp_format *sc;
	int err;

	/* media attributes */
	audio_sdp_attr_decode(call->audio);

	m = stream_sdpmedia(audio_strm(call->audio));

	sc = sdp_media_rformat(m, NULL);
	if (sc) {
		struct aucodec *ac = sc->data;

		if (ac) {
			err  = audio_encoder_set(call->audio, sc->data,
						 sc->pt, sc->params);
			if (err) {
				warning("nosip_call: start:"
					" audio_encoder_set error: %m\n",
					err);
			}
			err |= audio_decoder_set(call->audio, sc->data,
						 sc->pt, sc->params);
			if (err) {
				warning("nosip_call: start: audio_decoder_set"
					" error: %m\n",
					err);
			}

			if (!err) {
				err = audio_start(call->audio);
				if (err) {
					warning("nosip_call: start:"
						" audio_start error: %m\n",
						err);
				}
			}
		}
		else {
			info("nosip_call: no common audio-codecs..\n");
		}
	}
	else {
		info("nosip_call: audio stream is disabled..\n");
	}

	stream_update(audio_strm(call->audio));
}
