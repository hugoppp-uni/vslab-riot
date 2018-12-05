# name of your application
APPLICATION = vslab-riot

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../RIOT

# app specific configuration
NODES_NUM ?= 8
DEFAULT_CHANNEL ?= 11

USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += gcoap
USEMODULE += gnrc_icmpv6_echo
USEMODULE += gnrc_udp
USEMODULE += gnrc_sock_udp
USEMODULE += fmt
USEMODULE += xtimer

ifeq ($(BOARD),pba-d-01-kw2x)
	USEMODULE += hdc1000
endif

# set default channel for 802.15.4 devices
CFLAGS += -DIEEE802154_DEFAULT_CHANNEL=$(DEFAULT_CHANNEL)
# enable debug output, comment to disable
DEVELHELP ?= 1
CFLAGS += -DLOG_LEVEL=LOG_ALL
# adapt NODES_NUM above to match number of participants
CFLAGS += -DGCOAP_REQ_WAITING_MAX=$(NODES_NUM)
CFLAGS += -DELECT_NODES_NUM=$(NODES_NUM)
# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
