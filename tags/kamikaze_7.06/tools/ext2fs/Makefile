# 
# Copyright (C) 2006 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
# $Id$

include $(TOPDIR)/rules.mk

PKG_NAME:=genext2fs
PKG_VERSION:=1.4rc1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=@SF/$(PKG_NAME)
PKG_MD5SUM:=664431bf6737df1c265500e1f0b5d40c
PKG_CAT:=zcat

PKG_BUILD_DIR:=$(TOOL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/host-build.mk

define Build/Configure
	( cd $(PKG_BUILD_DIR); \
		./configure \
			--target=$(GNU_HOST_NAME) \
			--host=$(GNU_HOST_NAME) \
			--build=$(GNU_HOST_NAME) \
			--program-prefix="" \
			--program-suffix="" \
			--prefix=/usr \
			--exec-prefix=/usr \
			--bindir=/usr/bin \
			--sbindir=/usr/sbin \
			--libexecdir=/usr/lib \
			--sysconfdir=/etc \
			--datadir=/usr/share \
			--localstatedir=/var \
			--mandir=/usr/man \
			--infodir=/usr/info \
	)
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CFLAGS="-O2 -I $(STAGING_DIR)/include-host -include getline.h" \
		all
endef

define Build/Install
	mkdir -p $(STAGING_DIR)/bin
	install -m0755 $(PKG_BUILD_DIR)/genext2fs $(STAGING_DIR)/bin/
endef

define Build/Clean
	rm -f $(STAGING_DIR)/bin/genext2fs
endef

$(eval $(call HostBuild))
