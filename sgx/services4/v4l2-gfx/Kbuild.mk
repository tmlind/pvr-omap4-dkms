ccflags-y += -I$(TOP)/services4/v4l2-gfx

gfx_vout_mod-y += \
	services4/v4l2-gfx/gfx_init.o \
	services4/v4l2-gfx/gfx_io.o \
	services4/v4l2-gfx/gfx_bc.o \
	services4/v4l2-gfx/gfx_tiler.o
