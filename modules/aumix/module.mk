#
# module.mk
#
# Copyright (C) 2018 46labs
#

MOD		:= aumix
$(MOD)_SRCS	+= aumix.c
$(MOD)_SRCS	+= src.c
$(MOD)_SRCS	+= play.c
$(MOD)_SRCS	+= device.c

include mk/mod.mk
