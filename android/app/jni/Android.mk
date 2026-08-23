# Builds the Dinothawr libretro core (module "retro" -> libretro.so)
# together with the RetroArch frontend (libretroarch-activity.so) from
# an external RetroArch checkout.

TOP_LOCAL_PATH := $(call my-dir)

ifeq ($(RETROARCH_DIR),)
$(error RETROARCH_DIR is not set; it must point to a RetroArch checkout)
endif

include $(TOP_LOCAL_PATH)/../../../jni/Android.mk
include $(RETROARCH_DIR)/pkg/android/phoenix-common/jni/Android.mk
