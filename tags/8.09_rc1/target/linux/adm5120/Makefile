#
# Copyright (C) 2007-2008 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

BOARD:=adm5120
BOARDNAME:=Infineon/ADMtek ADM5120
LINUX_VERSION:=2.6.26.5
SUBTARGETS:=router_le router_be
INITRAMFS_EXTRA_FILES:=

include $(INCLUDE_DIR)/target.mk

DEFAULT_PACKAGES += admswconfig
DEFAULT_PACKAGES += \
	kmod-input-core \
	kmod-input-polldev \
	kmod-input-gpio-buttons \
	kmod-button-hotplug

$(eval $(call BuildTarget))
