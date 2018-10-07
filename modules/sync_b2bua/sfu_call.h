#include <re.h>

struct sfu_call;

int sfu_call_alloc(struct sfu_call **callp, const char *id, bool offer);
int sfu_call_accept(struct sfu_call *call, struct mbuf *desc, bool offer);
int sfu_call_sdp_debug(const struct sfu_call *call, bool offer);
int sfu_call_sdp_get(const struct sfu_call *call, struct mbuf **desc, bool offer);
int sfu_call_sdp_media_debug(const struct sfu_call *call);
int sfu_call_get_lrtp_parameters(struct sfu_call *call, struct odict **od);
int sfu_call_get_lrtp_transport(struct sfu_call *call, struct odict **od);
struct audio *sfu_call_audio(const struct sfu_call *call);
const char *sfu_call_id(const struct sfu_call *call);
void sfu_audio_start(const struct sfu_call *call);

