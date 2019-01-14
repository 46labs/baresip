/**
 * @file sync_b2bua/sync_b2bua.h Sync B2BUA
 *
 * Copyright (C) 2018 46labs
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

size_t session_count(void);

/*
 * Mixer source
 */

struct mixer_source {
	struct le le;
	struct nosip_call *nosip_call;
	struct device *dev;
};

int mixer_source_alloc(struct mixer_source **srcp, struct aumix *aumix,
		   const char *device, struct nosip_call *nosip_call,
		   struct audio* sip_call_audio);


/*
 * nosip_call
 */

struct nosip_call;

int nosip_call_alloc(struct nosip_call **callp, const char *id, bool offer);
int nosip_call_accept(struct nosip_call *call, struct mbuf *desc, bool offer);
int nosip_call_sdp_debug(const struct nosip_call *call, bool offer);
int nosip_call_sdp_get(const struct nosip_call *call, struct mbuf **desc,
		   bool offer);
int nosip_call_sdp_media_debug(const struct nosip_call *call);
struct audio *nosip_call_audio(const struct nosip_call *call);
const char *nosip_call_id(const struct nosip_call *call);
void nosip_audio_start(const struct nosip_call *call);


/*
 * Commands
 */

extern const struct cmd cmdv[];
extern const size_t command_count;

int nosip_call_create(struct mbuf **mb, const char *id,
		   const char *sip_callid);
int nosip_call_connect(const char *id, const char *sip_callid,
		   struct mbuf *mb);
int sip_call_hangup(const char *sip_callid, const char *reason);
int status(struct re_printf *pf);
int play_start(const char *sip_callid, const char *file, bool loop);
int play_stop(const char *sip_callid);
int play_list(struct odict *od_array);
int rtp_capabilities(struct re_printf *pf);
int mixer_source_add(struct mbuf **answer, const char *id,
		   const char *sip_callid, struct mbuf *offer);
int mixer_source_del(const char *id);
int mixer_source_enable(const char *id, const char *sip_callid);
int mixer_source_disable(const char *id);
int mixer_play(const char *file);
