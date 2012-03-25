ifeq ($(filter xorg,$(EXCLUDED_APIS)),)
 COMPONENTS += xorg pvr_conf pvr_video wsegl_dri2_linux
 -include ../common/apis/xorg_opengl.mk
endif
