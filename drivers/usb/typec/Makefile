ifeq ($(TARGET_BUILD_VARIANT),eng)
KBUILD_CFLAGS += -DASUS_ATD_INTERFACE -DDEBUG
endif

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
KBUILD_CFLAGS += -DDEBUG
endif

obj-$(CONFIG_USB_TYPEC_CORE) += usb_typec_phy.o \
						usb_typec_detect.o
obj-$(CONFIG_USB_TYPEC_PHY_FUSB300) += phy-fusb300.o
