/**
 * @file sync_b2bua/sync_b2bua.h Sync B2BUA
 *
 * Copyright (C) 2018 46labs
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

size_t sync_session_count(void);

/*
 * Mixer source
 */

struct mixer_source {
	struct le le;
	struct nosip_call *nosip_call;
	struct device *dev;
};

int sync_mixer_source_alloc(struct mixer_source **srcp, struct aumix *aumix,
		   const char *device, struct nosip_call *nosip_call,
		   struct audio* sip_call_audio);


/*
 * nosip_call
 */

struct nosip_call;

int sync_nosip_call_alloc(struct nosip_call **callp, const char *id, bool offer);
int sync_nosip_call_accept(struct nosip_call *call, struct mbuf *desc, bool offer);
int sync_nosip_call_sdp_debug(const struct nosip_call *call, bool offer);
int sync_nosip_call_sdp_get(const struct nosip_call *call, struct mbuf **desc,
		   bool offer);
int sync_nosip_call_sdp_media_debug(const struct nosip_call *call);
struct audio *sync_nosip_call_audio(const struct nosip_call *call);
const char *sync_nosip_call_id(const struct nosip_call *call);
void sync_nosip_audio_start(const struct nosip_call *call);


/*
 * Commands
 */

extern const struct cmd cmdv[];
extern const size_t command_count;

int sync_nosip_call_create(struct mbuf **mb, const char *id,
		   const char *sip_callid);
int sync_nosip_call_connect(const char *id, const char *sip_callid,
		   struct mbuf *mb);
int sync_sip_call_hangup(const char *sip_callid, const char *reason);
int sync_status(struct re_printf *pf);
int sync_play_start(const char *sip_callid, const char *file, bool loop);
int sync_play_stop(const char *sip_callid);
int sync_play_list(struct odict *od_array);
int sync_rtp_capabilities(struct re_printf *pf);
int sync_mixer_source_add(struct mbuf **answer, const char *id,
		   const char *sip_callid, struct mbuf *offer);
int sync_mixer_source_del(const char *id);
int sync_mixer_source_enable(const char *id, const char *sip_callid);
int sync_mixer_source_disable(const char *id);
int sync_mixer_play(const char *file);
