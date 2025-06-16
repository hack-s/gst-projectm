/*
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2019 Philippe Normand <philn@igalia.com>
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
 * The code in this file is based on code from
 * GStreamer / gst-plugins-base / 1.19.2: gst-libs/gst/gl/gstglbasesrc.c
 * Git Repository:
 * https://github.com/GStreamer/gst-plugins-base/blob/master/gst-libs/gst/gl/gstglbasesrc.c
 * Original copyright notice has been retained at the top of this file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglbaseaudiovisualizer.h"
#include "gstpmaudiovisualizer.h"
#include <gst/gl/gl.h>

/**
 * SECTION:GstGLBaseAudioVisualizer
 * @short_description: #GstPMAudioVisualizer subclass for injecting OpenGL
 * resources in a pipeline
 * @title: GstGLBaseAudioVisualizer
 * @see_also: #GstPMAudioVisualizer
 *
 * Wrapper for GstPMAudioVisualizer for handling OpenGL contexts.
 *
 * #GstGLBaseAudioVisualizer handles the nitty gritty details of retrieving an
 * OpenGL context. It also provides `gl_start()` and `gl_stop()` virtual methods
 * that ensure an OpenGL context is available and current in the calling thread
 * for initializing and cleaning up OpenGL resources. The `render`
 * virtual method of the GstPMAudioVisualizer is implemented to perform OpenGL
 * rendering. The implementer provides an implementation for fill_gl_memory to
 * render directly to gl memory.
 *
 * Typical plug-in call order for implementer-provided functions:
 * - setup (once)
 * - gl_start (once)
 * - fill_gl_memory (once for each frame)
 * - gl_stop (once)
 */

#define GST_CAT_DEFAULT gst_gl_base_audio_visualizer_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

#define DEFAULT_TIMESTAMP_OFFSET 0

struct _GstGLBaseAudioVisualizerPrivate {
  GstGLContext *other_context;
  GstGLMemory *out_tex;
  GstBuffer *in_audio;

  gint64 timestamp_offset;       /* base offset */
  gint64 n_frames;               /* total frames sent */
  GstClockTime buf_running_time; /* determined by no. of frames rendered. clock
                                    for buffer position. */

  gboolean gl_result;
  gboolean gl_started;

  GRecMutex context_lock;
};

/* Properties */
enum { PROP_0, PROP_TIMESTAMP_OFFSET };

#define gst_gl_base_audio_visualizer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE(
    GstGLBaseAudioVisualizer, gst_gl_base_audio_visualizer,
    GST_TYPE_PM_AUDIO_VISUALIZER,
    G_ADD_PRIVATE(GstGLBaseAudioVisualizer)
        GST_DEBUG_CATEGORY_INIT(gst_gl_base_audio_visualizer_debug,
                                "glbaseaudiovisualizer", 0,
                                "glbaseaudiovisualizer element"););

static void gst_gl_base_audio_visualizer_finalize(GObject *object);
static void gst_gl_base_audio_visualizer_set_property(GObject *object,
                                                      guint prop_id,
                                                      const GValue *value,
                                                      GParamSpec *pspec);
static void gst_gl_base_audio_visualizer_get_property(GObject *object,
                                                      guint prop_id,
                                                      GValue *value,
                                                      GParamSpec *pspec);

/* discover gl context / display from gst */
static void gst_gl_base_audio_visualizer_set_context(GstElement *element,
                                                     GstContext *context);
/* handle pipeline state changes */
static GstStateChangeReturn
gst_gl_base_audio_visualizer_change_state(GstElement *element,
                                          GstStateChange transition);

/* renders a video frame using gl, impl for parent class
 * GstPMAudioVisualizerClass. */
static gboolean gst_gl_base_audio_visualizer_parent_render(
    GstPMAudioVisualizer *bscope, GstBuffer *audio, GstVideoFrame *video);

/* internal utility for resetting state on start */
static void gst_gl_base_audio_visualizer_start(GstGLBaseAudioVisualizer *glav);

/* internal utility for cleaning up gl context on stop */
static void gst_gl_base_audio_visualizer_stop(GstGLBaseAudioVisualizer *glav);

/* gl memory pool allocation impl for parent class GstPMAudioVisualizerClass  */
static gboolean gst_gl_base_audio_visualizer_parent_decide_allocation(
    GstPMAudioVisualizer *gstav, GstQuery *query);

/* called when format changes, default empty v-impl for this class. can be
 * overwritten by implementer. */
static gboolean
gst_gl_base_audio_visualizer_default_setup(GstGLBaseAudioVisualizer *glav);

/* gl context is started and usable. called from gl thread. default empty v-impl
 * for this class, can be overwritten by implementer. */
static gboolean
gst_gl_base_audio_visualizer_default_gl_start(GstGLBaseAudioVisualizer *glav);

/* gl context is shutting down. called from gl thread. default empty v-impl for
 * this class. can be overwritten by implementer. */
static void
gst_gl_base_audio_visualizer_default_gl_stop(GstGLBaseAudioVisualizer *glav);

/* default empty v-impl for rendering a frame. called from gl thread. can be
 * overwritten by implementer. */
static gboolean gst_gl_base_audio_visualizer_default_fill_gl_memory(
    GstGLBaseAudioVisualizer *glav, GstBuffer *in_audio, GstGLMemory *mem);

/* find a valid gl context. lock must have already been acquired. */
static gboolean gst_gl_base_audio_visualizer_find_gl_context_unlocked(
    GstGLBaseAudioVisualizer *glav);

/* called whenever the format changes, impl for parent class
 * GstPMAudioVisualizerClass */
static gboolean
gst_gl_base_audio_visualizer_parent_setup(GstPMAudioVisualizer *gstav);

/* output buffer allocation default v-impl for this class. can be overwritten by
 * implementer. */
static GstFlowReturn gst_gl_base_audio_visualizer_default_prepare_output_buffer(
    GstGLBaseAudioVisualizer *scope, GstBuffer **outbuf);

/* output buffer allocation impl for parent class GstPMAudioVisualizerClass */
static GstFlowReturn gst_gl_base_audio_visualizer_parent_prepare_output_buffer(
    GstPMAudioVisualizer *scope, GstBuffer **outbuf);

/* map output video frame to buffer outbuf with gl flags, impl for parent class
 * GstPMAudioVisualizerClass */
static void gst_gl_base_audio_visualizer_parent_map_output_buffer(
    GstPMAudioVisualizer *scope, GstVideoFrame *outframe, GstBuffer *outbuf);

static void
gst_gl_base_audio_visualizer_class_init(GstGLBaseAudioVisualizerClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstPMAudioVisualizerClass *gstav_class = GST_PM_AUDIO_VISUALIZER_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

  gobject_class->finalize = gst_gl_base_audio_visualizer_finalize;
  gobject_class->set_property = gst_gl_base_audio_visualizer_set_property;
  gobject_class->get_property = gst_gl_base_audio_visualizer_get_property;

  element_class->set_context =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_set_context);

  element_class->change_state =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_change_state);

  gstav_class->decide_allocation =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_parent_decide_allocation);

  gstav_class->setup =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_parent_setup);

  gstav_class->render =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_parent_render);

  gstav_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(
      gst_gl_base_audio_visualizer_parent_prepare_output_buffer);

  gstav_class->map_output_buffer =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_parent_map_output_buffer);

  klass->supported_gl_api = GST_GL_API_ANY;

  klass->gl_start =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_default_gl_start);

  klass->gl_stop =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_default_gl_stop);

  klass->setup = GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_default_setup);

  klass->fill_gl_memory =
      GST_DEBUG_FUNCPTR(gst_gl_base_audio_visualizer_default_fill_gl_memory);

  klass->prepare_output_buffer = GST_DEBUG_FUNCPTR(
      gst_gl_base_audio_visualizer_default_prepare_output_buffer);

  g_object_class_install_property(
      gobject_class, PROP_TIMESTAMP_OFFSET,
      g_param_spec_int64("timestamp-offset", "Timestamp Offset",
                         "Specifies initial offset for the stream timestamp.",
                         0, G_MAXINT64, DEFAULT_TIMESTAMP_OFFSET,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void gst_gl_base_audio_visualizer_init(GstGLBaseAudioVisualizer *glav) {
  glav->priv = gst_gl_base_audio_visualizer_get_instance_private(glav);
  glav->priv->gl_started = FALSE;
  glav->priv->gl_result = TRUE;
  glav->priv->in_audio = NULL;
  glav->priv->out_tex = NULL;
  glav->context = NULL;
  glav->pts = 0;
  g_rec_mutex_init(&glav->priv->context_lock);
  gst_gl_base_audio_visualizer_start(glav);
}

static void gst_gl_base_audio_visualizer_finalize(GObject *object) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(object);
  gst_gl_base_audio_visualizer_stop(glav);

  g_rec_mutex_clear(&glav->priv->context_lock);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_gl_base_audio_visualizer_set_property(GObject *object,
                                                      guint prop_id,
                                                      const GValue *value,
                                                      GParamSpec *pspec) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(object);

  switch (prop_id) {

  case PROP_TIMESTAMP_OFFSET:
    glav->priv->timestamp_offset = g_value_get_int64(value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_gl_base_audio_visualizer_get_property(GObject *object,
                                                      guint prop_id,
                                                      GValue *value,
                                                      GParamSpec *pspec) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(object);

  switch (prop_id) {

  case PROP_TIMESTAMP_OFFSET:
    g_value_set_int64(value, glav->priv->timestamp_offset);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_gl_base_audio_visualizer_set_context(GstElement *element,
                                                     GstContext *context) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(element);
  GstGLBaseAudioVisualizerClass *klass =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);
  GstGLDisplay *old_display, *new_display;

  g_rec_mutex_lock(&glav->priv->context_lock);
  old_display = glav->display ? gst_object_ref(glav->display) : NULL;
  gst_gl_handle_set_context(element, context, &glav->display,
                            &glav->priv->other_context);
  if (glav->display)
    gst_gl_display_filter_gl_api(glav->display, klass->supported_gl_api);
  new_display = glav->display ? gst_object_ref(glav->display) : NULL;

  if (old_display && new_display) {
    if (old_display != new_display) {
      gst_clear_object(&glav->context);
      if (gst_gl_base_audio_visualizer_find_gl_context_unlocked(glav)) {
        gst_pad_mark_reconfigure(GST_BASE_SRC_PAD(glav));
      }
    }
  }
  gst_clear_object(&old_display);
  gst_clear_object(&new_display);
  g_rec_mutex_unlock(&glav->priv->context_lock);

  GST_ELEMENT_CLASS(parent_class)->set_context(element, context);
}

static gboolean
gst_gl_base_audio_visualizer_default_gl_start(GstGLBaseAudioVisualizer *glav) {
  return TRUE;
}

static gboolean
gst_gl_base_audio_visualizer_default_setup(GstGLBaseAudioVisualizer *glav) {
  return TRUE;
}

static void gst_gl_base_audio_visualizer_gl_start(GstGLContext *context,
                                                  gpointer data) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(data);
  GstGLBaseAudioVisualizerClass *glav_class =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);

  GST_INFO_OBJECT(glav, "starting");
  gst_gl_insert_debug_marker(glav->context, "starting element %s",
                             GST_OBJECT_NAME(glav));

  glav->priv->gl_started = glav_class->gl_start(glav);
}

static void
gst_gl_base_audio_visualizer_default_gl_stop(GstGLBaseAudioVisualizer *glav) {}

static void gst_gl_base_audio_visualizer_gl_stop(GstGLContext *context,
                                                 gpointer data) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(data);
  GstGLBaseAudioVisualizerClass *glav_class =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);

  GST_INFO_OBJECT(glav, "stopping");
  gst_gl_insert_debug_marker(glav->context, "stopping element %s",
                             GST_OBJECT_NAME(glav));

  if (glav->priv->gl_started)
    glav_class->gl_stop(glav);

  glav->priv->gl_started = FALSE;
}

static GstFlowReturn gst_gl_base_audio_visualizer_default_prepare_output_buffer(
    GstGLBaseAudioVisualizer *scope, GstBuffer **outbuf) {
  GstPMAudioVisualizer *pmav = GST_PM_AUDIO_VISUALIZER(scope);
  return gst_pm_audio_visualizer_default_prepare_output_buffer(pmav, outbuf);
}

static GstFlowReturn gst_gl_base_audio_visualizer_parent_prepare_output_buffer(
    GstPMAudioVisualizer *scope, GstBuffer **outbuf) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(scope);
  GstGLBaseAudioVisualizerClass *klass =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);
  return klass->prepare_output_buffer(glav, outbuf);
}

static void gst_gl_base_audio_visualizer_parent_map_output_buffer(
    GstPMAudioVisualizer *scope, GstVideoFrame *outframe, GstBuffer *outbuf) {
  /* map video to gl memory */
  gst_video_frame_map(outframe, &scope->vinfo, outbuf,
                      GST_MAP_WRITE | GST_MAP_GL |
                          GST_VIDEO_FRAME_MAP_FLAG_NO_REF);
}

static gboolean gst_gl_base_audio_visualizer_default_fill_gl_memory(
    GstGLBaseAudioVisualizer *glav, GstBuffer *in_audio, GstGLMemory *mem) {
  return TRUE;
}

static void _fill_gl(GstGLContext *context, GstGLBaseAudioVisualizer *glav) {
  GstGLBaseAudioVisualizerClass *klass =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);

  GST_TRACE_OBJECT(glav, "filling gl memory %p", glav->priv->out_tex);
  // inside gl thread: call virtual render function with audio and video
  glav->priv->gl_result =
      klass->fill_gl_memory(glav, glav->priv->in_audio, glav->priv->out_tex);
}

static GstFlowReturn
gst_gl_base_audio_visualizer_fill(GstPMAudioVisualizer *bscope,
                                  GstGLBaseAudioVisualizer *glav,
                                  GstBuffer *audio, GstVideoFrame *video) {
  GstClockTime next_time;
  GstGLSyncMeta *sync_meta;

  g_rec_mutex_lock(&glav->priv->context_lock);
  if (G_UNLIKELY(!glav->context))
    goto not_negotiated;

  /* 0 framerate and we are at the second frame, eos */
  if (G_UNLIKELY(GST_VIDEO_INFO_FPS_N(&bscope->vinfo) == 0 &&
                 glav->priv->n_frames == 1))
    goto eos;

  GstBuffer *buffer = video->buffer;

  // the following vars are params for passing values to _fill_gl()
  // video is mapped to gl memory
  glav->priv->out_tex = (GstGLMemory *)video->map[0].memory;
  glav->priv->in_audio = audio;

  // make current presentation timestamp accessible before rendering
  glav->pts = GST_BUFFER_PTS(buffer);

  // dispatch _fill_gl to the gl thread, blocking call
  gst_gl_context_thread_add(glav->context, (GstGLContextThreadFunc)_fill_gl,
                            glav);

  // clear param refs, these pointers never owned the data
  glav->priv->out_tex = NULL;
  glav->priv->in_audio = NULL;

  if (!glav->priv->gl_result)
    goto gl_error;

  sync_meta = gst_buffer_get_gl_sync_meta(buffer);
  if (sync_meta)
    gst_gl_sync_meta_set_sync_point(sync_meta, glav->context);

  g_rec_mutex_unlock(&glav->priv->context_lock);

  GST_BUFFER_TIMESTAMP(buffer) =
      glav->priv->timestamp_offset + glav->priv->buf_running_time;
  GST_BUFFER_OFFSET(buffer) = glav->priv->n_frames;
  glav->priv->n_frames++;
  GST_BUFFER_OFFSET_END(buffer) = glav->priv->n_frames;
  if (bscope->vinfo.fps_n) {
    next_time =
        gst_util_uint64_scale_int(glav->priv->n_frames * GST_SECOND,
                                  bscope->vinfo.fps_d, bscope->vinfo.fps_n);
    GST_BUFFER_DURATION(buffer) = next_time - glav->priv->buf_running_time;
  } else {
    next_time = glav->priv->timestamp_offset;
    /* NONE means forever */
    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
  }

  glav->priv->buf_running_time = next_time;

  return GST_FLOW_OK;

gl_error: {
  g_rec_mutex_unlock(&glav->priv->context_lock);
  GST_ELEMENT_ERROR(glav, RESOURCE, NOT_FOUND, (("failed to draw pattern")),
                    (("A GL error occurred")));
  return GST_FLOW_NOT_NEGOTIATED;
}
not_negotiated: {
  g_rec_mutex_unlock(&glav->priv->context_lock);
  GST_ELEMENT_ERROR(glav, CORE, NEGOTIATION, (NULL),
                    (("format wasn't negotiated before get function")));
  return GST_FLOW_NOT_NEGOTIATED;
}
eos: {
  g_rec_mutex_unlock(&glav->priv->context_lock);
  GST_DEBUG_OBJECT(glav, "eos: 0 framerate, frame %d",
                   (gint)glav->priv->n_frames);
  return GST_FLOW_EOS;
}
}

static gboolean
gst_gl_base_audio_visualizer_parent_setup(GstPMAudioVisualizer *gstav) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(gstav);
  GstGLBaseAudioVisualizerClass *glav_class =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(gstav);

  // cascade setup to the derived plugin after gl initialization has been
  // completed
  return glav_class->setup(glav);
}

static gboolean gst_gl_base_audio_visualizer_parent_render(
    GstPMAudioVisualizer *bscope, GstBuffer *audio, GstVideoFrame *video) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(bscope);

  gst_gl_base_audio_visualizer_fill(bscope, glav, audio, video);

  return glav->priv->gl_result;
}

static void gst_gl_base_audio_visualizer_start(GstGLBaseAudioVisualizer *glav) {
  glav->priv->n_frames = 0;
  glav->priv->buf_running_time = 0;
}

static void gst_gl_base_audio_visualizer_stop(GstGLBaseAudioVisualizer *glav) {
  g_rec_mutex_lock(&glav->priv->context_lock);

  if (glav->context) {
    if (glav->priv->gl_started)
      gst_gl_context_thread_add(glav->context,
                                gst_gl_base_audio_visualizer_gl_stop, glav);

    gst_object_unref(glav->context);
  }

  glav->context = NULL;
  g_rec_mutex_unlock(&glav->priv->context_lock);
}

static gboolean
_find_local_gl_context_unlocked(GstGLBaseAudioVisualizer *glav) {
  GstGLContext *context, *prev_context;
  gboolean ret;

  if (glav->context && glav->context->display == glav->display)
    return TRUE;

  context = prev_context = glav->context;
  g_rec_mutex_unlock(&glav->priv->context_lock);
  /* we need to drop the lock to query as another element may also be
   * performing a context query on us which would also attempt to take the
   * context_lock. Our query could block on the same lock in the other element.
   */
  ret = gst_gl_query_local_gl_context(GST_ELEMENT(glav), GST_PAD_SRC, &context);
  g_rec_mutex_lock(&glav->priv->context_lock);
  if (ret) {
    if (glav->context != prev_context) {
      /* we need to recheck everything since we dropped the lock and the
       * context has changed */
      if (glav->context && glav->context->display == glav->display) {
        if (context != glav->context)
          gst_clear_object(&context);
        return TRUE;
      }
    }

    if (context->display == glav->display) {
      glav->context = context;
      return TRUE;
    }
    if (context != glav->context)
      gst_clear_object(&context);
  }
  return FALSE;
}

static gboolean gst_gl_base_audio_visualizer_find_gl_context_unlocked(
    GstGLBaseAudioVisualizer *glav) {
  GstGLBaseAudioVisualizerClass *klass =
      GST_GL_BASE_AUDIO_VISUALIZER_GET_CLASS(glav);
  GError *error = NULL;
  gboolean new_context = FALSE;

  GST_DEBUG_OBJECT(
      glav, "attempting to find an OpenGL context, existing %" GST_PTR_FORMAT,
      glav->context);

  if (!glav->context)
    new_context = TRUE;

  if (!gst_gl_ensure_element_data(glav, &glav->display,
                                  &glav->priv->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api(glav->display, klass->supported_gl_api);

  _find_local_gl_context_unlocked(glav);

  if (!glav->context) {
    GST_OBJECT_LOCK(glav->display);
    do {
      if (glav->context) {
        gst_object_unref(glav->context);
        glav->context = NULL;
      }
      /* just get a GL context.  we don't care */
      glav->context =
          gst_gl_display_get_gl_context_for_thread(glav->display, NULL);
      if (!glav->context) {
        if (!gst_gl_display_create_context(glav->display,
                                           glav->priv->other_context,
                                           &glav->context, &error)) {
          GST_OBJECT_UNLOCK(glav->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context(glav->display, glav->context));
    GST_OBJECT_UNLOCK(glav->display);
  }
  GST_INFO_OBJECT(glav, "found OpenGL context %" GST_PTR_FORMAT, glav->context);

  if (new_context || !glav->priv->gl_started) {
    if (glav->priv->gl_started)
      gst_gl_context_thread_add(glav->context,
                                gst_gl_base_audio_visualizer_gl_stop, glav);

    {
      if ((gst_gl_context_get_gl_api(glav->context) &
           klass->supported_gl_api) == 0)
        goto unsupported_gl_api;
    }

    gst_gl_context_thread_add(glav->context,
                              gst_gl_base_audio_visualizer_gl_start, glav);

    if (!glav->priv->gl_started)
      goto error;
  }

  return TRUE;

unsupported_gl_api: {
  GstGLAPI gl_api = gst_gl_context_get_gl_api(glav->context);
  gchar *gl_api_str = gst_gl_api_to_string(gl_api);
  gchar *supported_gl_api_str = gst_gl_api_to_string(klass->supported_gl_api);
  GST_ELEMENT_ERROR(glav, RESOURCE, BUSY,
                    ("GL API's not compatible context: %s supported: %s",
                     gl_api_str, supported_gl_api_str),
                    (NULL));

  g_free(supported_gl_api_str);
  g_free(gl_api_str);
  return FALSE;
}
context_error: {
  if (error) {
    GST_ELEMENT_ERROR(glav, RESOURCE, NOT_FOUND, ("%s", error->message),
                      (NULL));
    g_clear_error(&error);
  } else {
    GST_ELEMENT_ERROR(glav, RESOURCE, NOT_FOUND, (NULL), (NULL));
  }
  if (glav->context)
    gst_object_unref(glav->context);
  glav->context = NULL;
  return FALSE;
}
error: {
  GST_ELEMENT_ERROR(glav, LIBRARY, INIT, ("Subclass failed to initialize."),
                    (NULL));
  return FALSE;
}
}

static gboolean gst_gl_base_audio_visualizer_parent_decide_allocation(
    GstPMAudioVisualizer *gstav, GstQuery *query) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(gstav);
  GstGLContext *context;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  g_rec_mutex_lock(&glav->priv->context_lock);
  if (!gst_gl_base_audio_visualizer_find_gl_context_unlocked(glav)) {
    g_rec_mutex_unlock(&glav->priv->context_lock);
    return FALSE;
  }
  context = gst_object_ref(glav->context);
  g_rec_mutex_unlock(&glav->priv->context_lock);

  gst_query_parse_allocation(query, &caps, NULL);

  if (gst_query_get_n_allocation_pools(query) > 0) {
    gst_query_parse_nth_allocation_pool(query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init(&vinfo);
    gst_video_info_from_caps(&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool || !GST_IS_GL_BUFFER_POOL(pool)) {
    /* can't use this pool */
    if (pool)
      gst_object_unref(pool);
    pool = gst_gl_buffer_pool_new(context);
  }
  config = gst_buffer_pool_get_config(pool);

  gst_buffer_pool_config_set_params(config, caps, size, min, max);
  gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (gst_query_find_allocation_meta(query, GST_GL_SYNC_META_API_TYPE, NULL))
    gst_buffer_pool_config_add_option(config,
                                      GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_add_option(
      config, GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);

  gst_buffer_pool_set_config(pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool(query, pool, size, min, max);

  gst_object_unref(pool);
  gst_object_unref(context);

  return TRUE;
}

static GstStateChangeReturn
gst_gl_base_audio_visualizer_change_state(GstElement *element,
                                          GstStateChange transition) {
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT(
      glav, "changing state: %s => %s",
      gst_element_state_get_name(GST_STATE_TRANSITION_CURRENT(transition)),
      gst_element_state_get_name(GST_STATE_TRANSITION_NEXT(transition)));

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
  case GST_STATE_CHANGE_READY_TO_NULL:
    g_rec_mutex_lock(&glav->priv->context_lock);
    gst_clear_object(&glav->priv->other_context);
    gst_clear_object(&glav->display);
    g_rec_mutex_unlock(&glav->priv->context_lock);
    break;
  default:
    break;
  }

  return ret;
}
