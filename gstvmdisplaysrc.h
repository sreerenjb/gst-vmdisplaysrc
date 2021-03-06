/* gst-vmdisplaysrc
 * Copyright (C) 2017 Intel Corporation
 *
 * Authors: 
 *   Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *   Philippuse, Philippe <.Lecluse@intel.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_VMDISPLAYSRC_H_
#define _GST_VMDISPLAYSRC_H_

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

#define GST_TYPE_VMDISPLAYSRC   (gst_vmdisplaysrc_get_type())
#define GST_VMDISPLAYSRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMDISPLAYSRC,GstVmdisplaysrc))
#define GST_VMDISPLAYSRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VMDISPLAYSRC,GstVmdisplaysrcClass))
#define GST_IS_VMDISPLAYSRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VMDISPLAYSRC))
#define GST_IS_VMDISPLAYSRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VMDISPLAYSRC))

typedef struct _GstVmdisplaysrc GstVmdisplaysrc;
typedef struct _GstVmdisplaysrcClass GstVmdisplaysrcClass;

struct _GstVmdisplaysrc
{
  GstPushSrc base_vmdisplaysrc;
  GstVideoInfo info;
  int pipe;
  int dom;
  int io_mode;
  int fd;
  GstAllocator* allocator;

};

struct _GstVmdisplaysrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_vmdisplaysrc_get_type (void);

G_END_DECLS

#endif
