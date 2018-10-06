/**
 * @file rtp_parameters.c RTP parameters
 *
 * Copyright (C) 2018 46labs
 */
#include <re.h>

struct audio;

int get_lrtp_parameters(struct audio *audio, struct odict **od);
