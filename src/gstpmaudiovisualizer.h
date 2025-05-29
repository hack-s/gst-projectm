/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) <2015> Luis de Bethencourt <luis@debethencourt.com>
 *
 * gstaudiovisualizer.c: base class for audio visualisation elements
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

/*
 * The code in this file is based on
 * GStreamer / gst-plugins-base / 1.19.2:
 * gst-libs/gst/pbutils/gstaudiovisualizer.h Git Repository:
 * https://github.com/GStreamer/gst-plugins-base/blob/master/gst-libs/gst/pbutils/gstaudiovisualizer.h
 * Original copyright notice has been retained at the top of this file.
 */

#ifndef __GST_PM_AUDIO_VISUALIZER_H__
#define __GST_PM_AUDIO_VISUALIZER_H__

#include <gst/gst.h>

#include <gst/audio/audio.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_PM_AUDIO_VISUALIZER (gst_pm_audio_visualizer_get_type())
#define GST_PM_AUDIO_VISUALIZER(obj)                                           \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PM_AUDIO_VISUALIZER,             \
                              GstPMAudioVisualizer))
#define GST_PM_AUDIO_VISUALIZER_CLASS(klass)                                   \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PM_AUDIO_VISUALIZER,              \
                           GstPMAudioVisualizerClass))
#define GST_PM_AUDIO_VISUALIZER_GET_CLASS(obj)                                 \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_PM_AUDIO_VISUALIZER,              \
                             GstPMAudioVisualizerClass))
#define GST_PM_IS_SYNAESTHESIA(obj)                                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PM_AUDIO_VISUALIZER))
#define GST_PM_IS_SYNAESTHESIA_CLASS(klass)                                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PM_AUDIO_VISUALIZER))
typedef struct _GstPMAudioVisualizer GstPMAudioVisualizer;
typedef struct _GstPMAudioVisualizerClass GstPMAudioVisualizerClass;
typedef struct _GstPMAudioVisualizerPrivate GstPMAudioVisualizerPrivate;

struct _GstPMAudioVisualizer {
  GstElement parent;

  /* min samples per frame wanted by the subclass (one channel) */
  guint req_spf;

  /* video state */
  GstVideoInfo vinfo;

  /* audio state */
  GstAudioInfo ainfo;

  /* current time (ns) position within the input stream */
  guint64 stream_time;

  /*< private >*/
  gpointer _padding[GST_PADDING];

  GstPMAudioVisualizerPrivate *priv;
};

/**
 * GstPMAudioVisualizerClass:
 * @decide_allocation: buffer pool allocation
 * @prepare_output_buffer: allocate a buffer for rendering a frame.
 * @map_output_buffer: map video frame to memory buffer.
 * @render: render a frame from an audio buffer.
 * @setup: called whenever the format changes.
 *
 * Base class for audio visualizers, derived from gstreamer
 * GstAudioVisualizerClass. This plugin handles rendering video frames with a
 * fixed framerate from audio input samples.
 */
struct _GstPMAudioVisualizerClass {
  /*< private >*/
  GstElementClass parent_class;

  /*< public >*/
  /* virtual function, called whenever the format changes */
  gboolean (*setup)(GstPMAudioVisualizer *scope);

  /* virtual function for rendering a frame */
  gboolean (*render)(GstPMAudioVisualizer *scope, GstBuffer *audio,
                     GstVideoFrame *video);

  /* virtual function for buffer pool allocation  */
  gboolean (*decide_allocation)(GstPMAudioVisualizer *scope, GstQuery *query);

  /* virtual function for output buffer allocation */
  GstFlowReturn (*prepare_output_buffer)(GstPMAudioVisualizer *scope,
                                         GstBuffer **outbuf);

  /* virtual function for mapping the output buffer to video frame */
  void (*map_output_buffer)(GstPMAudioVisualizer *scope,
                            GstVideoFrame *outframe, GstBuffer *outbuf);
};

GType gst_pm_audio_visualizer_get_type(void);

GstFlowReturn gst_pm_audio_visualizer_default_prepare_output_buffer(
    GstPMAudioVisualizer *scope, GstBuffer **outbuf);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstPMAudioVisualizer, gst_object_unref)

G_END_DECLS
#endif /* __GST_PM_AUDIO_VISUALIZER_H__ */
