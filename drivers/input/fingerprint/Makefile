#
# Makefile for all kinds of Fingerprint
#

obj-$(CONFIG_GOODIX_CAP_FINGERPRINT) += goodix_cap/
obj-$(CONFIG_FPC_FINGERPRINT) += fpc/fpc1022_tee.o
subdir-ccflags-y += -I$(srctree)/drivers/tee/teei/400/tz_driver/include/
subdir-ccflags-y += -I$(srctree)/drivers/tee/teei/400/common/include/

# The fingerprint sources are shared by both Xiaomi image variants.  Chopin
# retains the 4.14 userspace key-event and display-wakeup ABI.
ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"mediatek/chopin")
subdir-ccflags-y += -D_XIAOMI_CHOPIN_
endif
