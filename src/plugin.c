#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_GLEW
#include <GL/glew.h>
#endif

#include "plugin.h"

#include <gst/gl/gstglfuncs.h>
#include <gst/gst.h>

#include "caps.h"
#include "debug.h"
#include "gstglbaseaudiovisualizer.h"

GST_DEBUG_CATEGORY_STATIC(gst_projectm_debug);
#define GST_CAT_DEFAULT gst_projectm_debug

struct _GstProjectMPrivate {

  GstBaseProjectMPrivate base;

  GstBuffer *in_audio;
  GstGLMemory *mem;
};

G_DEFINE_TYPE_WITH_CODE(GstProjectM, gst_projectm,
                        GST_TYPE_GL_BASE_AUDIO_VISUALIZER,
                        G_ADD_PRIVATE(GstProjectM)
                            GST_DEBUG_CATEGORY_INIT(gst_projectm_debug,
                                                    "gstprojectm", 0,
                                                    "Plugin Root"));

void gst_projectm_set_property(GObject *object, guint property_id,
                               const GValue *value, GParamSpec *pspec) {

  GstProjectM *plugin = GST_PROJECTM(object);

  gst_projectm_base_set_property(object, &plugin->settings, property_id, value,
                                 pspec);
}

void gst_projectm_get_property(GObject *object, guint property_id,
                               GValue *value, GParamSpec *pspec) {
  GstProjectM *plugin = GST_PROJECTM(object);

  gst_projectm_base_get_property(object, &plugin->settings, property_id, value,
                                 pspec);
}

static void gst_projectm_init(GstProjectM *plugin) {
  plugin->priv = gst_projectm_get_instance_private(plugin);

  gst_gl_memory_init_once();

  gst_projectm_base_init(&plugin->settings, &plugin->priv->base);

  plugin->priv->in_audio = NULL;
  plugin->priv->mem = NULL;
}

static void gst_projectm_finalize(GObject *object) {

  GstProjectM *plugin = GST_PROJECTM(object);

  gst_projectm_base_finalize(&plugin->settings, &plugin->priv->base);
  G_OBJECT_CLASS(gst_projectm_parent_class)->finalize(object);
}

static void gst_projectm_gl_stop(GstGLBaseAudioVisualizer *src) {

  GstProjectM *plugin = GST_PROJECTM(src);

  GST_PROJECTM_BASE_LOCK(plugin);

  gst_projectm_base_gl_stop(G_OBJECT(src), &plugin->priv->base);

  GST_PROJECTM_BASE_UNLOCK(plugin);
}

static gboolean gst_projectm_gl_start(GstGLBaseAudioVisualizer *glav) {
  // Cast the audio visualizer to the ProjectM plugin
  GstProjectM *plugin = GST_PROJECTM(glav);
  GstPMAudioVisualizer *gstav = GST_PM_AUDIO_VISUALIZER(glav);

  GST_PROJECTM_BASE_LOCK(plugin);

  gst_projectm_base_gl_start(G_OBJECT(glav), &plugin->priv->base,
                             &plugin->settings, glav->context, &gstav->vinfo);

  GST_PROJECTM_BASE_UNLOCK(plugin);

  GST_INFO_OBJECT(plugin, "GL start complete");

  return TRUE;
}

static gboolean gst_projectm_setup(GstGLBaseAudioVisualizer *glav) {

  GstPMAudioVisualizer *gstav = GST_PM_AUDIO_VISUALIZER(glav);

  // Log audio info
  GST_DEBUG_OBJECT(
      glav, "Audio Information <Channels: %d, SampleRate: %d, Description: %s>",
      gstav->ainfo.channels, gstav->ainfo.rate,
      gstav->ainfo.finfo->description);

  // Log video info
  GST_DEBUG_OBJECT(
      glav,
      "Video Information <Dimensions: %dx%d, FPS: %d/%d, SamplesPerFrame: %d>",
      GST_VIDEO_INFO_WIDTH(&gstav->vinfo), GST_VIDEO_INFO_HEIGHT(&gstav->vinfo),
      gstav->vinfo.fps_n, gstav->vinfo.fps_d, gstav->req_spf);

  return TRUE;
}

static gboolean gst_projectm_fill_gl_memory_callback(gpointer stuff) {

  GstProjectM *plugin = GST_PROJECTM(stuff);
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(stuff);
  gboolean result = TRUE;

  // VIDEO
  GST_TRACE_OBJECT(plugin, "rendering projectM to fbo %d",
                   plugin->priv->base.fbo->fbo_id);

  /*
  const GstGLFuncs *glFunctions = glav->context->gl_vtable;

  GLuint tex_id = gst_gl_memory_get_texture_id(plugin->priv->mem);

  glFunctions->BindFramebuffer(GL_FRAMEBUFFER, plugin->priv->base.fbo->fbo_id);
  glFunctions->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
  GL_TEXTURE_2D, tex_id, 0); glFunctions->BindTexture(GL_TEXTURE_2D, 0);
*/
  gst_projectm_base_fill_gl_memory_callback(
      GST_OBJECT(plugin), &plugin->priv->base, &plugin->settings, glav->context,
      glav->pts, plugin->priv->in_audio);

  // GST_DEBUG_OBJECT(plugin, "Video Data: %d %d\n",
  // GST_VIDEO_FRAME_N_PLANES(video), ((uint8_t
  // *)(GST_VIDEO_FRAME_PLANE_DATA(video, 0)))[0]);

  // GST_DEBUG_OBJECT(plugin, "Rendered one frame");

  return result;
}

static gboolean gst_projectm_fill_gl_memory(GstGLBaseAudioVisualizer *glav,
                                            GstBuffer *in_audio,
                                            GstGLMemory *mem) {

  GstProjectM *plugin = GST_PROJECTM(glav);

  GST_PROJECTM_BASE_LOCK(plugin);

  plugin->priv->in_audio = in_audio;
  plugin->priv->mem = mem;

  gboolean result = gst_gl_framebuffer_draw_to_texture(
      plugin->priv->base.fbo, mem, gst_projectm_fill_gl_memory_callback,
      plugin);

  plugin->priv->in_audio = NULL;
  plugin->priv->mem = NULL;

  GST_PROJECTM_BASE_UNLOCK(plugin);

  return result;
}

static void gst_projectm_class_init(GstProjectMClass *klass) {
  GObjectClass *gobject_class = (GObjectClass *)klass;
  GstElementClass *element_class = (GstElementClass *)klass;
  GstGLBaseAudioVisualizerClass *scope_class =
      GST_GL_BASE_AUDIO_VISUALIZER_CLASS(klass);

  // Setup audio and video caps
  const gchar *audio_sink_caps = get_audio_sink_cap();
  const gchar *video_src_caps = get_video_src_cap();

  gst_element_class_add_pad_template(
      GST_ELEMENT_CLASS(klass),
      gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                           gst_caps_from_string(video_src_caps)));
  gst_element_class_add_pad_template(
      GST_ELEMENT_CLASS(klass),
      gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                           gst_caps_from_string(audio_sink_caps)));

  gst_element_class_set_static_metadata(
      GST_ELEMENT_CLASS(klass), "ProjectM Visualizer", "Generic",
      "A plugin for visualizing music using ProjectM",
      "AnomieVision <anomievision@gmail.com> | Tristan Charpentier "
      "<tristan_charpentier@hotmail.com>");

  // Setup properties
  gobject_class->set_property = gst_projectm_set_property;
  gobject_class->get_property = gst_projectm_get_property;

  gst_projectm_base_install_properties(gobject_class);

  gobject_class->finalize = gst_projectm_finalize;

  scope_class->supported_gl_api = GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
  scope_class->gl_start = GST_DEBUG_FUNCPTR(gst_projectm_gl_start);
  scope_class->gl_stop = GST_DEBUG_FUNCPTR(gst_projectm_gl_stop);
  scope_class->fill_gl_memory = GST_DEBUG_FUNCPTR(gst_projectm_fill_gl_memory);
  scope_class->setup = GST_DEBUG_FUNCPTR(gst_projectm_setup);
}
