/**
 * @file rtp_parameters.c RTP parameters
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <baresip.h>

#include "../src/core.h"
#include "rtp_parameters.h"

#define FOREACH_ELEMENT(dict)					\
	for (le = dict->u.odict->lst.head; le; le=le->next)

/* external declarations */
int sdp_format_radd(struct sdp_media *m, const struct pl *id);
struct sdp_format *sdp_format_find(const struct list *lst, const struct pl *id);

static const char *uri_aulevel = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";

/**
 * encode opus codec parameters
 *
 * based on modules/opus.c
 */
static int encode_opus_paramameters(struct odict *od)
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

/**
 * decode opus codec parameters
 *
 * based on modules/opus.c
 */
static int decode_opus_paramameters(char *params, size_t len, const struct odict_entry *od)
{
	char *p;
	int n = 0;
	struct odict_entry *oe;
	struct le *le;

	p = params;

	FOREACH_ELEMENT(od) {
		oe = (struct odict_entry*)le->data;

		if (!str_len(params)) {
			n = re_snprintf(p, len - str_len(p),
					"%s=%d", oe->key, oe->u.integer);
		}
		else {
			n = re_snprintf(p, len - str_len(p),
					";%s=%d", oe->key, oe->u.integer);
		}

		if (n <= 0)
			return ENOMEM;

		p += n;
	}

	return 0;
}

static int validate_codec(struct odict *od)
{
	if (!odict_lookup(od, "channels") ||
			odict_lookup(od, "channels")->type != ODICT_INT)
		return EINVAL;

	if (!odict_lookup(od, "clockRate") ||
			odict_lookup(od, "clockRate")->type != ODICT_INT)
		return EINVAL;

	if (!odict_lookup(od, "mimeType") ||
			odict_lookup(od, "mimeType")->type != ODICT_STRING)
		return EINVAL;

	if (!odict_lookup(od, "name") ||
			odict_lookup(od, "name")->type != ODICT_STRING)
		return EINVAL;

	if (!odict_lookup(od, "parameters") ||
			odict_lookup(od, "parameters")->type != ODICT_OBJECT)
		return EINVAL;

	if (!odict_lookup(od, "payloadType") ||
			odict_lookup(od, "payloadType")->type != ODICT_INT)
		return EINVAL;

	if (!odict_lookup(od, "rtpFeedback") ||
			odict_lookup(od, "rtpFeedback")->type != ODICT_ARRAY)
		return EINVAL;

	return 0;
}

int validate_rtp_parameters(const struct odict *od)
{
	const struct odict_entry *codecs, *encodings, *header_extensions, *rtcp;
	struct le *le;
	int err;

	// validate codecs.
	codecs = odict_lookup(od, "codecs");
	if (!codecs || codecs->type != ODICT_ARRAY)
		return EINVAL;

	FOREACH_ELEMENT(codecs) {
		err = validate_codec(((struct odict_entry*)le->data)->u.odict);
		if (err) {
			warning("invalid codec entry\n");
			return EINVAL;
		}
	}

	// validate encodings.
	encodings = odict_lookup(od, "encodings");
	if (!encodings || encodings->type != ODICT_ARRAY)
		return EINVAL;

	FOREACH_ELEMENT(encodings) {
	}

	// validate header extensions.
	header_extensions = odict_lookup(od, "headerExtensions");
	if (!header_extensions || header_extensions->type != ODICT_ARRAY)
		return EINVAL;

	FOREACH_ELEMENT(header_extensions) {
	}

	// validate rtcp.
	rtcp = odict_lookup(od, "rtcp");
	if (!rtcp || rtcp->type != ODICT_OBJECT)
		return EINVAL;


	return 0;
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
			err |= encode_opus_paramameters(parameters);

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
	const struct list *rfmtl;
	struct le *le;
	int err = 0;

	m = stream_sdpmedia(audio_strm(audio));

	// get remote sdp format list.
	rfmtl = sdp_media_format_lst(m, false /*local*/);

	// reset current format entries.
	list_flush((struct list*)rfmtl);

	// add 'fmt' entries.
	FOREACH_ELEMENT(odict_lookup(od, "codecs")) {
		struct odict *codec = ((struct odict_entry*)le->data)->u.odict;
		struct sdp_format *fmt;
		struct pl pl;
		char pt[4];
		char params[255];

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

		// fill parameters.
		if (!str_cmp(fmt->name, "opus")) {
			err |= decode_opus_paramameters(
					params, sizeof(params), odict_lookup(codec, "parameters"));
			if (err)
				goto out;

			sdp_format_set_params(fmt, params);
		}
	}

 out:
	// mem_deref created fmt entries.
	if (err)
		list_flush((struct list*)rfmtl);

		return err;
}
