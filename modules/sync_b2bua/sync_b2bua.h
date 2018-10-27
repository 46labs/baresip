#include <re.h>
#include <rem.h>
#include <baresip.h>


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

int nosip_call_create(struct re_printf *pf, const char *id,
		   const char *sip_callid);
int nosip_call_connect(struct re_printf *pf, const char *id,
		   const char *sip_callid, const char *desc);
int sip_call_hangup(struct re_printf *pf, const char *sip_callid,
		   const char *reason);
int status(struct re_printf *pf);
int play_start(struct re_printf *pf, const char *sip_callid,
		   const char *file, bool loop);
int play_stop(struct re_printf *pf, const char *sip_callid);
int play_list(struct re_printf *pf);
int rtp_capabilities(struct re_printf *pf);
int mixer_source_add(struct re_printf *pf, const char *id,
		   const char *sip_callid, const char *desc);
int mixer_source_del(struct re_printf *pf, const char *id);
