#include <baresip.h>

struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct ausrc_device_st *dev;
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void *arg;
};

