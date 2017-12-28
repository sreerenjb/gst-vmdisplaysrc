lib_LTLIBRARIES = libgstvmdisplaysrc.la

noinst_HEADERS = gstvmdisplaysrc.h

libgstvmdisplaysrc_la_SOURCES = gstvmdisplaysrc.c

libgstvmdisplaysrc_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(GST_ALLOCATORS_CFLAGS) \
	$(KMS_DRM_CFLAGS) \
	$(NULL)

libgstvmdisplaysrc_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(GST_ALLOCATORS_LIBS) \
	$(KMS_DRM_LIBS) \
	$(NULL)

libdir = ${exec_prefix}/lib/gstreamer-1.0
