# name of your application
APPLICATION = plain_udp

# If no BOARD is found in the environment, use this default:
BOARD ?= iotlab-m3

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
#CFLAGS += -DDEVELHELP
#CFLAGS += -DTEST_SUITES
#CFLAGS += -DSCHEDSTATISTICS

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present

USEMODULE += auto_init_gnrc_netif

# Specify the mandatory networking modules for socket communication via UDP
USEMODULE += gnrc_ipv6_router_default

USEMODULE += gnrc_udp


# main stacksize needs to be increased for large packets
export CFLAGS += -DTHREAD_STACKSIZE_MAIN=4096

ifneq (,$(LOOPBACK_MODE))
	# 0: L2 reflector mode, 1: IPv6 Loopback mode
	#LOOPBACK_MODE ?= 1
	export CFLAGS += -DLOOPBACK_MODE=$(LOOPBACK_MODE)
endif
ifneq (1, $(LOOPBACK_MODE))
		USEMODULE += l2_reflector
endif

include $(RIOTBASE)/Makefile.include
