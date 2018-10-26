#
# module.mk
#
# Copyright (C) 2018 46labs
#

MOD		:= sync_b2bua
$(MOD)_SRCS	+= sync_b2bua.c nosip_call.c mixer_source.c device.c src.c play.c

include mk/mod.mk
