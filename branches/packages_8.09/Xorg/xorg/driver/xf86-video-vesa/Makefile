#
# Copyright (C) 2007-2008 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
# $Id$

include $(TOPDIR)/rules.mk

PKG_NAME:=xf86-video-vesa
PKG_RELEASE:=4
PKG_VERSION:=2.0.0

PKG_SOURCE_URL:=http://xorg.freedesktop.org/releases/X11R7.4/src/driver
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_BUILD_DIR=$(BUILD_DIR)/Xorg/$(_CATEGORY)/$(PKG_NAME)-$(PKG_VERSION)/
PKG_MD5SUM:=433cf6f961eb4a2ab6fcb086def0b2b4
PKG_BUILD_DEPENDS:=xproto fontsproto randrproto renderproto xextproto
PKG_INSTALL:=1

include $(INCLUDE_DIR)/package.mk


define Package/xf86-video-vesa
  SECTION:=xorg-driver
  CATEGORY:=Xorg
  SUBMENU:=driver
  DEPENDS:=+xserver-xorg +libpciaccess
  TITLE:=xf86-video-vesa
  URL:=http://xorg.freedesktop.org/
endef

define Package/xf86-video-vesa/install
	$(INSTALL_DIR) $(1)/usr/lib/xorg/modules/drivers
	$(INSTALL_DATA) \
		$(PKG_INSTALL_DIR)/usr/lib/xorg/modules/drivers/vesa_drv.so \
		$(1)/usr/lib/xorg/modules/drivers/
endef

$(eval $(call BuildPackage,xf86-video-vesa))
