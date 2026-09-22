#
# Makefile for ST21NFC
#

ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
# some older platforms may need ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include

ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"mediatek/chopin")
ccflags-y += -D_XIAOMI_CHOPIN_
endif

obj-$(CONFIG_NFC_CHIP_SUPPORT) +=st21nfc.o
ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
