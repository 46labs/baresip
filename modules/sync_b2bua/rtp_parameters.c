/**
 * @file rtp_parameters.c RTP parameters
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <baresip.h>

#include "../src/core.h"
#include "rtp_parameters.h"

/* external declarations */
int sdp_format_radd(struct sdp_media *m, const struct pl *id);
struct sdp_format *sdp_format_find(const struct list *lst, const struct pl *id);

static const char *uri_aulevel = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";

/**
 * Opus codec parameters
 *
 * based on modules/opus.c
 */
static int get_opus_paramameters(struct odict *od)
{
	struct conf *conf = conf_cur();
	uint32_t value;
	bool b, stereo = true, sprop_stereo = true;
	int err;

	conf_get_bool(conf, "opus_stereo", &stereo);
	conf_get_bool(conf, "opus_sprop_stereo", &sprop_stereo);

	err = odict_entry_add(od, "stereo", ODICT_INT, stereo);
	err |= odict_entry_add(od, "sprop-stereo", ODICT_INT, sprop_stereo);

	if (0 == conf_get_u32(conf, "opus_bitrate", &value))
		err |= odict_entry_add(od, "maxaveragebitrate", ODICT_INT, value);

	if (0 == conf_get_bool(conf, "opus_cbr", &b))
		err |= odict_entry_add(od, "cbr", ODICT_INT, b);

	if (0 == conf_get_bool(conf, "opus_inbandfec", &b))
		err |= odict_entry_add(od, "useinbandfec", ODICT_INT, b);

	if (0 == conf_get_bool(conf, "opus_dtx", &b))
		err |= odict_entry_add(od, "usedtx", ODICT_INT, b);

	return err;
}

int get_lrtp_parameters(struct audio *audio, struct odict **od_rtp_params)
{
	struct sdp_media *m;
	struct odict *od;
	struct odict *codecs, *encodings, *header_extensions, *rtcp;
	const struct list *fmtl;
	struct le *le;
	int err;

	m = stream_sdpmedia(audio_strm(audio));

	err = odict_alloc(&od, 4);
	err |= odict_alloc(&codecs, 8);
	err |= odict_alloc(&encodings, 8);
	err |= odict_alloc(&header_extensions, 8);
	err |= odict_alloc(&rtcp, 8);
	if (err)
		goto out;

	err |= odict_entry_add(od, "codecs", ODICT_ARRAY, codecs);
	err |= odict_entry_add(od, "encodings", ODICT_ARRAY, encodings);
	err |= odict_entry_add(od, "headerExtensions", ODICT_ARRAY, header_extensions);
	err |= odict_entry_add(od, "rtcp", ODICT_OBJECT, rtcp);
	if (err)
		goto out;

	// generate 'codecs' entry.
	fmtl = sdp_media_format_lst(m, true /* local */);

	for (le=fmtl->head; le; le=le->next)
	{
		struct sdp_format *fmt = le->data;
		struct odict *codec, *parameters, *rtcp_feedback;
		char mime_type[32];

		re_snprintf(mime_type, sizeof(mime_type), "audio/%s", fmt->name);

		err = odict_alloc(&codec, 8);
		err |= odict_alloc(&parameters, 8);
		err |= odict_alloc(&rtcp_feedback, 8);

		err |= odict_entry_add(codec, "channels", ODICT_INT, fmt->ch);
		err |= odict_entry_add(codec, "clockRate", ODICT_INT, fmt->srate);
		err |= odict_entry_add(codec, "mimeType", ODICT_STRING, mime_type);
		err |= odict_entry_add(codec, "name", ODICT_STRING, fmt->name);
		err |= odict_entry_add(codec, "parameters", ODICT_OBJECT, parameters);
		err |= odict_entry_add(codec, "payloadType", ODICT_INT, fmt->pt);
		err |= odict_entry_add(codec, "rtcpFeedback", ODICT_ARRAY, rtcp_feedback);

		// fill parameters.
		if (!str_cmp(fmt->name, "opus"))
			err |= get_opus_paramameters(parameters);

		err |= odict_entry_add(codecs, "", ODICT_OBJECT, codec);

		mem_deref(codec);
		mem_deref(parameters);
		mem_deref(rtcp_feedback);

		if (err)
			goto out;
	}

	// generate 'encodings' entry.
	{
		struct odict *encoding;
		uint32_t ssrc = rtp_sess_ssrc(audio_strm(audio)->rtp);

		err = odict_alloc(&encoding, 1);
		err |= odict_entry_add(encoding, "ssrc", ODICT_INT, ssrc);

		err |= odict_entry_add(encodings, "", ODICT_OBJECT, encoding);

		mem_deref(encoding);

		if (err)
			goto out;
	}

	// generate 'headerExtensions' entry.
	{
		const struct config *cfg = conf_config();
		struct odict *header_extension;

		err = odict_alloc(&header_extension, 8);
		if (err)
			goto out;

		// audio level.
		if (cfg->audio.level)
		{
			err |= odict_entry_add(header_extension, "id", ODICT_INT, 1);
			err |= odict_entry_add(header_extension, "uri", ODICT_STRING, uri_aulevel);

			err |= odict_entry_add(header_extensions, "", ODICT_OBJECT, header_extension);

			mem_deref(header_extension);

			if (err)
				goto out;
		}
	}

	// generate 'rtcp' entry.
	{
		const struct config *cfg = conf_config();
		char* cname = audio_strm(audio)->cname;
		bool rtcp_mux = cfg->avt.rtcp_mux ? true : false;
		bool rtcp_rsize = cfg->avt.rtcp_enable ? true : false;

		err |= odict_entry_add(rtcp, "cname", ODICT_STRING, cname);
		err |= odict_entry_add(rtcp, "mux", ODICT_BOOL, rtcp_mux);
		err |= odict_entry_add(rtcp, "reducedSize", ODICT_BOOL, rtcp_rsize);

		if (err)
			goto out;
	}

 out:
	mem_deref(codecs);
	mem_deref(encodings);
	mem_deref(header_extensions);
	mem_deref(rtcp);

	if (!err)
		*od_rtp_params = od;

	return err;
}

int set_rrtp_parameters(struct audio *audio, const struct odict *od)
{
	struct sdp_media *m;
	const struct odict *codecs, *encodings, *header_extensions, *rtcp;
	const struct odict_entry *oe_c, *oe_e, *oe_he, *oe_rtcp;
	const struct list *rfmtl;
	struct le *le;
	int err = 0;

	// validate parameters.
	oe_c = odict_lookup(od, "codecs");
	oe_e = odict_lookup(od, "encodings");
	oe_he = odict_lookup(od, "headerExtensions");
	oe_rtcp = odict_lookup(od, "rtcp");

	if (!oe_c|| !oe_e|| !oe_he || !oe_rtcp) {
		warning("invalid rtp parameters");
		return EINVAL;
	}

	codecs = oe_c->u.odict;
	encodings = oe_e->u.odict;
	header_extensions = oe_he->u.odict;
	rtcp = oe_rtcp->u.odict;

	m = stream_sdpmedia(audio_strm(audio));

	// get remote sdp format list.
	rfmtl = sdp_media_format_lst(m, false /*local*/);

	// reset current format entries.
	list_flush((struct list*)rfmtl);

	// add 'fmt' entries.
	for (le=codecs->lst.head; le; le=le->next) {

		struct sdp_format *fmt;
		struct odict *codec = ((struct odict_entry*)le->data)->u.odict;
		struct pl pl;
		char pt[4];

		if (!odict_lookup(codec, "payloadType")) {
			err = EINVAL;
			goto out;
		}

		re_snprintf(pt, sizeof(pt), "%d",
				(int)odict_lookup(codec, "payloadType")->u.integer);

		pl.p = pt;
		pl.l = str_len(pt);

		sdp_format_radd(m, &pl);
		fmt = sdp_format_find(rfmtl, &pl);
		if (!fmt) {
			err = EINVAL;
			goto out;
		}

		str_dup(&fmt->name, odict_lookup(codec, "name")->u.str);

		fmt->ch = odict_lookup(codec, "channels")->u.integer;
		fmt->srate = (uint32_t)odict_lookup(codec, "clockRate")->u.integer;
		fmt->pt = (int)odict_lookup(codec, "payloadType")->u.integer;

		// TODO: complete fmt->parameters by iterating code.parameters
		// We need to know the type of each parameter.
		// Special behaviour must be taken for each codec.
	}

 out:
	// mem_deref created fmt entries.
	if (err)
		list_flush((struct list*)rfmtl);

		return err;
}
