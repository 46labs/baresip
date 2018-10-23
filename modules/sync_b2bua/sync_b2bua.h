#include <re.h>
#include <rem.h>
#include <baresip.h>


/*
 * Mixer auplay
 */

int mixer_auplay_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg);

/*
 * Mixer ausrc
 */
int mixer_ausrc_alloc(struct ausrc_st **stp, const struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);

/*
 * Mixer source
 */

struct mixer_source {
	struct le le;
	struct nosip_call *nosip_call;          /**> nosip_call */
	struct auplay_device_st *auplay_device; /**< Aumix source */
	struct aumix_source *aumix_source;      /**< Aumix source */
};

int mixer_source_alloc(struct mixer_source **srcp, struct aumix *aumix,
		   char *device, struct nosip_call *nosip_call, struct audio* sip_call_audio);


/*
 * Mixer auplay device
 */

struct auplay_device_st;

int auplay_device_alloc(struct auplay_device_st **stp, const char *device,
		   struct aumix_source *asrc);
struct auplay_device_st *auplay_device_find(const char *device);
struct aumix_source *auplay_device_aumix_src(struct auplay_device_st *device);


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
