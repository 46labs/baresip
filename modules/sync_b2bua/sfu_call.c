/**
 * @file sfu_call.c SFU call state
 *
 * Copyright (C) 2018 46labs
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

#include "../src/core.h"
#include "sfu_call.h"
#include "rtp_parameters.h"

struct sfu_call {
	char *id;                /**< SFU call id */
	struct sdp_session *sdp; /**< SDP Session  */
	struct audio *audio;     /**< Audio stream */
};

static void sfu_call_destructor(void *arg)
{
	struct sfu_call *call = arg;

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

int sfu_call_sdp_get(const struct sfu_call *call, struct mbuf **desc, bool offer)
{
	int err;

	err = sdp_encode(desc, call->sdp, offer);
	if (err) {
		warning("sfu_call: sdp_encode failed (%m)\n", err);
		goto out;
	}

 out:
	return err;
}

int sfu_call_sdp_debug(const struct sfu_call *call, bool offer)
{
	struct mbuf *desc;
	int err;

	err = sfu_call_sdp_get(call, &desc, offer);
	if (err) {
		goto out;
	}

	info("- - - - - S D P - %s - - - - -\n"
	     "%b"
	     "- - - - - - - - - - - - - - - - - - -\n",
	     offer ? "O f f e r" : "A n s w e r", desc->buf, desc->end);

	mem_deref(desc);

 out:
	return err;
}

int sfu_call_sdp_media_debug(const struct sfu_call *call)
{
	struct mbuf *mb = mbuf_alloc(2048);
	struct re_printf pf = {print_handler, mb};
	int err;

	err = sdp_media_debug(&pf, stream_sdpmedia(audio_strm(call->audio)));

	info("%b", mb->buf, mb->end);

	mem_deref(mb);

	return err;
}

int sfu_call_get_lrtp_parameters(struct sfu_call *call, struct odict **od)
{
	return get_lrtp_parameters(call->audio, od);
}

int sfu_call_get_lrtp_transport(struct sfu_call *call, struct odict **od_rtp_transport)
{
	const struct network *net = baresip_network();
	char addr[64];
	struct odict *od;
	struct sa laddr;
	struct sdp_media *m;
	int err;

	err = odict_alloc(&od, 2);
	if (err)
		return err;

	sa_cpy(&laddr, net_laddr_af(net, net_af(net)));

	// retrieve IP address.
	err |= sa_ntop(&laddr, addr, sizeof(addr));
	err |= odict_entry_add(od, "ip", ODICT_STRING, addr);
	if (err)
		goto out;

	// retrieve port.
	m = stream_sdpmedia(audio_strm(call->audio));
	err |= odict_entry_add(od, "port", ODICT_INT, sa_port(sdp_media_laddr(m)));

 out:
	if (err)
		mem_deref(od);

	if (!err)
		*od_rtp_transport = od;

	return err;
}

/**
 * Allocate a new SFU Call state object
 *
 * @param callp       Pointer to allocated SFU Call state object
 * @param offer     Boolean
 *
 * @return 0 if success, otherwise errorcode
 */
int sfu_call_alloc(struct sfu_call **callp, const char* id, bool offer)
{
	const struct network *net = baresip_network();
	const struct config *cfg = conf_config();
	struct sfu_call *call;
	struct stream_param stream_prm;
	struct sa laddr;
	int err;

	debug("sfu_call_alloc\n");

	memset(&stream_prm, 0, sizeof(stream_prm));
	stream_prm.use_rtp = true;

	call = mem_zalloc(sizeof(*call), sfu_call_destructor);
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
			NULL /* audio_event_h */, NULL /* audio_err_h */, call);
	if (err) {
		warning("sfu_call: audio_alloc failed (%m)\n", err);
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
 * @param sfu_call  SFU Call object
 *
 * @return Audio object
 */
struct audio *sfu_call_audio(const struct sfu_call *call)
{
	debug("sfu_call_audio\n");

	return call ? call->audio : NULL;
}

const char *sfu_call_id(const struct sfu_call *call)
{
	return call ? call->id : NULL;
}


/**
 * Accept a call. Provide the remote SDP
 */
int sfu_call_accept(struct sfu_call *call, struct odict *od)
{
	int err;

	debug("sfu_call_accept\n");

	err = set_rrtp_parameters(call->audio, od);
	if (err) {
		warning("b2bua: set_rrtp_parameters failed (%m)\n", err);
		goto out;
	}

	// TMP
	sfu_call_sdp_media_debug(call);

	sfu_audio_start(call);

 out:
	return err;
}

/**
 * Start audio object
 */
void sfu_audio_start(const struct sfu_call *call)
{
	const struct sdp_format *sc;
	int err;

	debug("sfu_audio_start\n");

	/* media attributes */
	audio_sdp_attr_decode(call->audio);

	sc = sdp_media_rformat(stream_sdpmedia(audio_strm(call->audio)), NULL);
	if (sc) {
		struct aucodec *ac = sc->data;

		if (ac) {
			err  = audio_encoder_set(call->audio, sc->data, sc->pt, sc->params);
			if (err) {
				warning("call: start: audio_encoder_set error: %m\n", err);
			}
			err |= audio_decoder_set(call->audio, sc->data, sc->pt, sc->params);
			if (err) {
				warning("call: start: audio_decoder_set error: %m\n", err);
			}

			if (!err) {
				err = audio_start(call->audio);
				if (err) {
					warning("call: start: audio_start error: %m\n", err);
				}
			}
		}
		else {
			info("call: no common audio-codecs..\n");
		}
	}
	else {
		info("call: audio stream is disabled..\n");
	}

	stream_update(audio_strm(call->audio));
}
