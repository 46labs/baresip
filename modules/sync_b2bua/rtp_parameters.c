/**
 * @file rtp_parameters.c RTP parameters
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>
#include <baresip.h>

#include "../src/core.h"
#include "rtp_parameters.h"

static const char *uri_aulevel = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";

int get_lrtp_parameters(struct audio *audio, struct odict **od_rtp_params)
{
	struct sdp_media *m;
	struct odict *od;
	struct odict *codecs, *encodings, *header_extensions, *rtcp;
	const struct list *fmtl;
	struct le *le;
	int err;

	m = stream_sdpmedia(audio_strm(audio));

	err = odict_alloc(&od, 64);
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

		err = odict_alloc(&codec, 16);
		err |= odict_alloc(&parameters, 8);
		err |= odict_alloc(&rtcp_feedback, 8);

		err |= odict_entry_add(codec, "channels", ODICT_INT, fmt->ch);
		err |= odict_entry_add(codec, "clockRate", ODICT_INT, fmt->srate);
		err |= odict_entry_add(codec, "mimeType", ODICT_STRING, mime_type);
		err |= odict_entry_add(codec, "name", ODICT_STRING, fmt->name);
		err |= odict_entry_add(codec, "parameters", ODICT_OBJECT, parameters);
		err |= odict_entry_add(codec, "payloadType", ODICT_INT, fmt->pt);
		err |= odict_entry_add(codec, "rtcpFeedback", ODICT_ARRAY, rtcp_feedback);

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

		err = odict_alloc(&encoding, 16);
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

		err = odict_alloc(&header_extension, 16);
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
