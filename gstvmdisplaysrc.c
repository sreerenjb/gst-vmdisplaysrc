/* gst-vmdisplaysrc
 * Copyright (C) 2017 Intel Corporation
 *
 * Authors: 
 *   Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *   Lecluse, Philippe <philippe.lecluse@intel.com>
 *   Hatcher, Philip <philip.hatcher@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstvmdisplaysrc
 *
 * The vmdisplaysrc element is a simple source for zero copy
 * from vmdisplay dmabuf to other gstreamer elements.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v vmdisplaysrc dom=1 pipe=0 ! "video/x-raw,format=BGRx,width=3440,height=1440" ! vaapipostproc format=nv12 !  vaapih264enc tune=low-power ! filesink location=t1.h264
 * ]|
 * Encodes the frames from a vm pipe0 on dom1.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvmdisplaysrc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <drm/drm.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifndef DRM_RDWR
#define DRM_RDWR O_RDWR
#endif

//#define USE_GVT

#ifdef USE_GVT
typedef int32_t s32;
#include <igvtg-kernel-headers/i915_drm.h>
#include <igvtg-kernel-headers/drm.h>
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
static int g_Dbg = 0;
static int last_handle = 0;
#endif

#define GST_VMDISPLAYSRC_FORMATS "{ BGRx, RGBx }"

static const char gst_vmdisplay_src_caps_str[] = "video/x-raw,"
    "format = " GST_VMDISPLAYSRC_FORMATS ","
    "framerate = (fraction)[0/1, 2147483647/1],"
    "width = (int)[1, 2147483647]," "height = (int)[1, 2147483647]";

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vmdisplay_src_caps_str));

enum
{
  GST_VMDISPLAYSRC_IO_MODE_DMABUF_EXPORT = 1,
  GST_VMDISPLAYSRC_IO_MODE_DMABUF_IMPORT = 2,
};

#define GST_VMDISPLAYSRC_IO_MODE_TYPE (gst_vmdisplaysrc_io_mode_get_type())
static GType
gst_vmdisplaysrc_io_mode_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {GST_VMDISPLAYSRC_IO_MODE_DMABUF_EXPORT, "Export dmabuf", "dmabuf-export"},
    {GST_VMDISPLAYSRC_IO_MODE_DMABUF_IMPORT, "Import dmabuf", "dmabuf-import"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstVMDisplayIOMode", values);
  }
  return type;
}

extern int drmModeAddFB (int fd, uint32_t width, uint32_t height, uint8_t depth,
    uint8_t bpp, uint32_t pitch, uint32_t bo_handle, uint32_t * buf_id);
int drmIoctl (int fd, unsigned long request, void *arg);
int
drmIoctl (int fd, unsigned long request, void *arg)
{
  int ret;
  do {
    ret = ioctl (fd, request, arg);
  } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
  return ret;
}

GST_DEBUG_CATEGORY_STATIC (gst_vmdisplaysrc_debug);
#define GST_CAT_DEFAULT gst_vmdisplaysrc_debug

enum
{
  PROP_0,
  PROP_IO_MODE,
  PROP_DOM,
  PROP_PIPE,
  PROP_LAST
};

static void gst_vmdisplaysrc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vmdisplaysrc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static gboolean gst_vmdisplaysrc_start (GstBaseSrc * src);
static gboolean gst_vmdisplaysrc_stop (GstBaseSrc * src);
static GstFlowReturn gst_vmdisplaysrc_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_vmdisplaysrc_is_seekable (GstBaseSrc * src);
static gboolean gst_vmdisplaysrc_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static void gst_vmdisplaysrc_finalize (GObject * object);

#define _do_init \
	  GST_DEBUG_CATEGORY_INIT (gst_vmdisplaysrc_debug, "vmdisplaysrc", 0, "vmdisplaysrc element");
#define gst_vmdisplaysrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVmdisplaysrc, gst_vmdisplaysrc, GST_TYPE_PUSH_SRC,
    _do_init);


static void
gst_vmdisplaysrc_class_init (GstVmdisplaysrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_vmdisplaysrc_set_property;
  gobject_class->get_property = gst_vmdisplaysrc_get_property;
  gobject_class->finalize = gst_vmdisplaysrc_finalize;

  g_object_class_install_property (gobject_class, PROP_DOM,
      g_param_spec_int ("dom", "Dom number",
          "DOM to be used for display scraping",
          0, 4, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIPE,
      g_param_spec_int ("pipe", "Pipe number",
          "Pipe to be used for display scraping",
          0, 4, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IO_MODE,
      g_param_spec_enum ("io-mode", "io-mode",
          "I/O mode ", gst_vmdisplaysrc_io_mode_get_type (),
          GST_VMDISPLAYSRC_IO_MODE_DMABUF_EXPORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VMDisplay source", "Source/Video",
      "Zero copy source plugin for VMDisplay scraping",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>, "
      "Lecluse, Philippe <philippe.lecluse@intel.com>, "
      "Hatcher, Philip <philip.hatcher@intel.com>");

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &srctemplate);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_vmdisplaysrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_vmdisplaysrc_stop);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_vmdisplaysrc_is_seekable);
  gstpush_src_class->create = GST_DEBUG_FUNCPTR (gst_vmdisplaysrc_create);
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_vmdisplaysrc_setcaps);
}

static void
gst_vmdisplaysrc_init (GstVmdisplaysrc * vmdisplaysrc)
{
  vmdisplaysrc->io_mode = GST_VMDISPLAYSRC_IO_MODE_DMABUF_EXPORT;
  gst_video_info_init (&vmdisplaysrc->info);
  vmdisplaysrc->allocator = gst_dmabuf_allocator_new ();
}

void
gst_vmdisplaysrc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (object);

  GST_DEBUG_OBJECT (vmdisplaysrc, "set_property");

  switch (property_id) {
    case PROP_DOM:
      vmdisplaysrc->dom = g_value_get_int (value);
      break;
    case PROP_PIPE:
      vmdisplaysrc->pipe = g_value_get_int (value);
      break;
    case PROP_IO_MODE:
      vmdisplaysrc->io_mode = g_value_get_enum (value);
      /* Fixme: probably we only need dmabuf export ? */
      if (vmdisplaysrc->io_mode != GST_VMDISPLAYSRC_IO_MODE_DMABUF_EXPORT) {
        GST_WARNING_OBJECT (vmdisplaysrc,
            "dmabuf export is the only supported mode!");
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_vmdisplaysrc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (object);

  GST_DEBUG_OBJECT (vmdisplaysrc, "get_property");

  switch (property_id) {
    case PROP_DOM:
      g_value_set_int (value, vmdisplaysrc->dom);
      break;
    case PROP_PIPE:
      g_value_set_int (value, vmdisplaysrc->pipe);
      break;
    case PROP_IO_MODE:
      g_value_set_enum (value, vmdisplaysrc->io_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


/* start and stop processing, ideal for opening/closing the resource */
static gboolean
gst_vmdisplaysrc_start (GstBaseSrc * src)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (src);

  GST_DEBUG_OBJECT (vmdisplaysrc, "start");

  if (!vmdisplaysrc->allocator) {
    GST_ERROR_OBJECT (vmdisplaysrc, "No gst-dmabuf Allocator!!");
    return FALSE;
  }

  vmdisplaysrc->fd = open ("/dev/dri/card0", O_RDWR);
  if (vmdisplaysrc->fd < 0) {
    GST_ERROR ("Couldn't open /dev/dri/card0");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vmdisplaysrc_stop (GstBaseSrc * src)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (src);

  GST_DEBUG_OBJECT (vmdisplaysrc, "stop");

  if (vmdisplaysrc->fd)
    close (vmdisplaysrc->fd);

  //PPP -- Need to free up some stuff here??
  return TRUE;
}

static gboolean
gst_vmdisplaysrc_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (bsrc);
  GstStructure *structure;
  GstVideoInfo info;

  structure = gst_caps_get_structure (caps, 0);
  GST_DEBUG_OBJECT (vmdisplaysrc, "Getting currently set caps%" GST_PTR_FORMAT,
      caps);
  if (gst_structure_has_name (structure, "video/x-raw")) {
    /* we can use the parsing code */
    if (!gst_video_info_from_caps (&info, caps))
      goto parse_failed;
  }

  vmdisplaysrc->info = info;

  return TRUE;

  /* ERRORS */
parse_failed:
  {
    GST_DEBUG_OBJECT (bsrc, "failed to parse caps");
    return FALSE;
  }

}

static gboolean
gst_vmdisplaysrc_is_seekable (GstBaseSrc * basesrc)
{
  return FALSE;
}

static void
gst_vmdisplaysrc_finalize (GObject * object)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (object);
  if (vmdisplaysrc->allocator)
    gst_object_unref (vmdisplaysrc->allocator);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#ifdef USE_GVT

void
close_gem_handle (GstVmdisplaysrc * vmdisplaysrc, uint32_t handle)
{
  if (!handle) {
    printf ("CloseGemHandle on handle 0 !\n");
    return;
  }
  struct drm_gem_close drm_close_bo;
  memset (&drm_close_bo, 0, sizeof (drm_close_bo));
  drm_close_bo.handle = handle;
  drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_GEM_CLOSE, &drm_close_bo);
}


#define ALIGN(x, y) ((x + y - 1) & ~(y - 1))

struct drm_i915_gem_gvtbuffer *
get_new_primary_buffer (GstVmdisplaysrc * vmdisplaysrc, uint32_t * h)
{
  static int old_start = 0;
  static struct drm_i915_gem_gvtbuffer vcreate;
  memset (&vcreate, 0, sizeof (struct drm_i915_gem_gvtbuffer));
  vcreate.id = vmdisplaysrc->dom;
  vcreate.plane_id = I915_GVT_PLANE_PRIMARY;
  vcreate.phys_pipe_id = UINT_MAX;
  vcreate.pipe_id = vmdisplaysrc->pipe;
  vcreate.flags = I915_GVTBUFFER_QUERY_ONLY;
  drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_I915_GEM_GVTBUFFER, &vcreate);
  if (!vcreate.start)
    return NULL;
  if (vcreate.start != old_start) {
    if (last_handle) {
      close_gem_handle (vmdisplaysrc, last_handle);
      last_handle = 0;
    }

    vcreate.flags = 0;
    struct drm_i915_gem_gvtbuffer *vc = &vcreate;
    if (g_Dbg) {
      printf ("GVTBUFFER: %dx%d %d [%d] F=0x%x T=%d s=%d\n", vc->width,
          vc->height, vc->bpp, vc->stride, vc->drm_format, vc->tiled, vc->size);
    }
    unsigned int aligned_height =
        ALIGN (vcreate.height, ((vc->tiled == 4) ? 32 : 8));
    unsigned int ggtt_size = (vc->stride * aligned_height);
    vcreate.size = (ggtt_size + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
    if (g_Dbg > 2) {
      printf ("Fix size: %d\n", vcreate.size);
    }
    drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_I915_GEM_GVTBUFFER, &vcreate);
    old_start = vcreate.start;
#if 1
    if (h) {
      int r;
      uint32_t handle;
      r = drmPrimeHandleToFD (vmdisplaysrc->fd, vcreate.handle,
          DRM_CLOEXEC | DRM_RDWR, &handle);
      if (g_Dbg) {
        printf ("drmPrimeHandleToFD = %d - 0x%x - h=%d\n", r, handle,
            vcreate.handle);
      }
      last_handle = vcreate.handle;
      *h = handle;
    }
#endif
    return &vcreate;
  }
  return NULL;
}

#endif

/* ask the subclass to fill the buffer with data from offset and size */
static GstFlowReturn
gst_vmdisplaysrc_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstVmdisplaysrc *vmdisplaysrc = GST_VMDISPLAYSRC (psrc);
  GstVideoInfo *info = &vmdisplaysrc->info;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  gint aligned_stride = 0;

  GST_DEBUG_OBJECT (vmdisplaysrc, "create");

  if (GST_VIDEO_INFO_FORMAT (info) != GST_VIDEO_FORMAT_RGBx &&
      GST_VIDEO_INFO_FORMAT (info) != GST_VIDEO_FORMAT_BGRx) {
    GST_ERROR_OBJECT (vmdisplaysrc, "Unsupported video format");
    return GST_FLOW_ERROR;
  }

  /* Should handle this in a better way, outside of create() ! */
  aligned_stride = GST_ROUND_UP_32 (info->width) * 4;
  stride[0] = aligned_stride;

#ifndef USE_GVT
  {
    GstMemory *myMem;
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb mreq;
    struct drm_prime_handle preq;
    uint32_t fb;
    int ret;
    void *map;

    /* create dumb buffer */
    memset (&creq, 0, sizeof (creq));
    creq.width = info->width;
    creq.height = info->height;
    creq.bpp = 32;

    ret = drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
      GST_ERROR_OBJECT (vmdisplaysrc, "DRM Create Dumb failed ");
      return GST_FLOW_ERROR;
      /* buffer creation failed; see "errno" for more error codes */
    }
    /* creq.pitch, creq.handle and creq.size are filled by this ioctl with
     * the requested values and can be used now. */

    /* create framebuffer object for the dumb-buffer */
    ret =
        drmModeAddFB (vmdisplaysrc->fd, info->width, info->height, 24, 32,
        creq.pitch, creq.handle, &fb);
    if (ret) {
      /* frame buffer creation failed; see "errno" */
      GST_ERROR_OBJECT (vmdisplaysrc, "AddfD failed");
      return GST_FLOW_ERROR;
    }
    /* the framebuffer "fb" can now used for scanout with KMS */

    /* prepare buffer for memory mapping */
    memset (&mreq, 0x00, sizeof (mreq));
    mreq.handle = creq.handle;
    ret = drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret) {
      /* DRM buffer preparation failed; see "errno" */
      GST_ERROR_OBJECT (vmdisplaysrc, "drm map_dumb failed\n");
      return GST_FLOW_ERROR;
    }
    /* mreq.offset now contains the new offset that can be used with mmap() */

    /* perform actual memory mapping */
    map =
        mmap (0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
        vmdisplaysrc->fd, mreq.offset);
    if (map == MAP_FAILED) {
      /* memory-mapping failed; see "errno" */
      GST_ERROR_OBJECT (vmdisplaysrc, "mapping failed");
      return GST_FLOW_ERROR;
    }

    /* clear the framebuffer to 0 */
    memset (map, 0xFF, creq.size);
    munmap (map, creq.size);

    preq.handle = mreq.handle;
    preq.flags = DRM_CLOEXEC | DRM_RDWR;

    ret = drmIoctl (vmdisplaysrc->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &preq);
    if (ret < 0) {
      /* buffer creation failed; see "errno" for more error codes */
      GST_ERROR_OBJECT (vmdisplaysrc, "PRIME_HANDLE_TO_FD Failed");
      return GST_FLOW_ERROR;
    } else
      GST_DEBUG_OBJECT (vmdisplaysrc, "FD is %d (s= 0x%x)\n", preq.fd,
          (gint) creq.size);

    myMem =
        gst_dmabuf_allocator_alloc (vmdisplaysrc->allocator, preq.fd,
        creq.size);
    *outbuf = gst_buffer_new ();
    gst_buffer_append_memory (*outbuf, myMem);
    gst_buffer_add_video_meta_full (*outbuf, 0, GST_VIDEO_INFO_FORMAT (info),
        info->width, info->height, 1, offset, stride);
  }

#else
  {
    GstMemory *myMem;
    struct drm_i915_gem_gvtbuffer *v;
    uint32_t h;
    int waitcycle = 0;
    unsigned int aligned_height;
    unsigned int gtt_size;

    do {
      v = get_new_primary_buffer (vmdisplaysrc, &h);
      if (!v) {
        g_usleep (5 * 1000);
        waitcycle++;
      }
    } while (v == NULL);

    aligned_height = ALIGN (v->height, ((v->tiled == 4) ? 32 : 8));
    gtt_size = (v->stride * v->height);
    GST_DEBUG_OBJECT (vmdisplaysrc, "pass after %d Prime: %d S=0x%x  H=%d\n",
        waitcycle, h, gtt_size, v->handle);

    if (h) {
      myMem = gst_dmabuf_allocator_alloc (vmdisplaysrc->allocator, h, gtt_size);
      *outbuf = gst_buffer_new ();
      gst_buffer_append_memory (*outbuf, myMem);
      gst_buffer_add_video_meta_full (*outbuf, 0, GST_VIDEO_INFO_FORMAT (info),
          info->width, info->height, 1, offset, stride);
    } else {
      GST_ERROR_OBJECT (vmdisplaysrc,
          "Bad handle:  after %d Prime: %d S=0x%x  H=%d\n", waitcycle, h,
          gtt_size, v->handle);
      return GST_FLOW_ERROR;
    }
  }
#endif

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "vmdisplaysrc", GST_RANK_NONE,
      GST_TYPE_VMDISPLAYSRC);
}

#ifndef VERSION
#define VERSION "0.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "gst-vmdisplay"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "gst-vmdisplay"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.intel.com"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vmdisplaysrc,
    "Zero copy VM display scraping",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
