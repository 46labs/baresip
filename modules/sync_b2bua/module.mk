#
# module.mk
#
# Copyright (C) 2018 46labs
#

MOD		:= sync_b2bua
$(MOD)_SRCS	+= sync_b2bua.c nosip_call.c mixer_source.c mixer_auplay.c mixer_auplay_device.c mixer_ausrc.c mixer_ausrc_device.c

include mk/mod.mk
