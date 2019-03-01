#
# module.mk
#
# Copyright (C) 2018 46labs
#

MOD		:= sync_b2bua
$(MOD)_SRCS	+= sync_b2bua.c
$(MOD)_SRCS	+= commands.c
$(MOD)_SRCS	+= nosip_call.c
$(MOD)_SRCS	+= mixer_source.c

include mk/mod.mk
