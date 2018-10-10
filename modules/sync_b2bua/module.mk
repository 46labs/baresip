#
# module.mk
#
# Copyright (C) 2018 46labs
#

MOD		:= sync_b2bua
$(MOD)_SRCS	+= sync_b2bua.c nosip_call.c

include mk/mod.mk
