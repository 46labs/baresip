#include <re.h>

struct nosip_call;

int nosip_call_alloc(struct nosip_call **callp, const char *id, bool offer);
int nosip_call_accept(struct nosip_call *call, struct mbuf *desc, bool offer);
int nosip_call_sdp_debug(const struct nosip_call *call, bool offer);
int nosip_call_sdp_get(const struct nosip_call *call, struct mbuf **desc, bool offer);
int nosip_call_sdp_media_debug(const struct nosip_call *call);
struct audio *nosip_call_audio(const struct nosip_call *call);
const char *nosip_call_id(const struct nosip_call *call);
void nosip_audio_start(const struct nosip_call *call);

