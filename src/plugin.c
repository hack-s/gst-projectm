#include <projectM-4/parameters.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_GLEW
#include <GL/glew.h>
#endif
#include <gst/gl/gstglfuncs.h>
#include <gst/gst.h>
#include <gst/pbutils/gstaudiovisualizer.h>

#include <projectM-4/projectM.h>

#include "caps.h"
#include "config.h"
#include "debug.h"
#include "enums.h"
#include "gstglbaseaudiovisualizer.h"
#include "plugin.h"
#include "projectm.h"

GST_DEBUG_CATEGORY_STATIC(gst_projectm_debug);
#define GST_CAT_DEFAULT gst_projectm_debug

struct _GstProjectMPrivate {
  projectm_handle handle;

  GstGLFramebuffer *fbo;
  GLuint textureID;
  GstBuffer *in_audio;
  GstGLMemory *mem;
  GstGLVideoAllocationParams *allocation_params;
};

G_DEFINE_TYPE_WITH_CODE(GstProjectM, gst_projectm,
                        GST_TYPE_GL_BASE_AUDIO_VISUALIZER,
                        G_ADD_PRIVATE(GstProjectM)
                            GST_DEBUG_CATEGORY_INIT(gst_projectm_debug,
                                                    "gstprojectm", 0,
                                                    "Plugin Root"));

static GstBuffer *wrap_gl_texture(GstGLBaseAudioVisualizer *glav,
                                  GstProjectM *plugin) {
  GstGLMemoryAllocator *allocator;
  gpointer wrapped[1];
  GstGLFormat formats[1];
  GstBuffer *buffer;
  gboolean ret;

  allocator = gst_gl_memory_allocator_get_default(glav->context);

  buffer = gst_buffer_new();
  if (!buffer) {
    g_error("Failed to create new buffer\n");
    return NULL;
  }

  wrapped[0] = (gpointer)plugin->priv->textureID;
  formats[0] = GST_GL_RGBA8;

  // * Wrap the texture into GLMemory. *
  ret = gst_gl_memory_setup_buffer(
      allocator, buffer, plugin->priv->allocation_params, formats, wrapped, 1);
  if (!ret) {
    g_error("Failed to setup gl memory\n");
    return NULL;
  }

  gst_object_unref(allocator);

  return buffer;
}

static GstFlowReturn
gst_projectm_prepare_output_buffer(GstGLBaseAudioVisualizer *scope,
                                   GstBuffer **outbuf) {
  GstProjectM *plugin = GST_PROJECTM(scope);

  *outbuf = wrap_gl_texture(scope, plugin);
  GST_INFO_OBJECT(plugin, "Wrapped RT texture buffer");
  return GST_FLOW_OK;
}

void gst_projectm_set_property(GObject *object, guint property_id,
                               const GValue *value, GParamSpec *pspec) {
  GstProjectM *plugin = GST_PROJECTM(object);

  const gchar *property_name = g_param_spec_get_name(pspec);
  GST_DEBUG_OBJECT(plugin, "set-property <%s>", property_name);

  switch (property_id) {
  case PROP_PRESET_PATH:
    plugin->preset_path = g_strdup(g_value_get_string(value));
    break;
  case PROP_TEXTURE_DIR_PATH:
    plugin->texture_dir_path = g_strdup(g_value_get_string(value));
    break;
  case PROP_BEAT_SENSITIVITY:
    plugin->beat_sensitivity = g_value_get_float(value);
    break;
  case PROP_HARD_CUT_DURATION:
    plugin->hard_cut_duration = g_value_get_double(value);
    break;
  case PROP_HARD_CUT_ENABLED:
    plugin->hard_cut_enabled = g_value_get_boolean(value);
    break;
  case PROP_HARD_CUT_SENSITIVITY:
    plugin->hard_cut_sensitivity = g_value_get_float(value);
    break;
  case PROP_SOFT_CUT_DURATION:
    plugin->soft_cut_duration = g_value_get_double(value);
    break;
  case PROP_PRESET_DURATION:
    plugin->preset_duration = g_value_get_double(value);
    break;
  case PROP_MESH_SIZE: {
    const gchar *meshSizeStr = g_value_get_string(value);
    gint width, height;

    gchar **parts = g_strsplit(meshSizeStr, ",", 2);

    if (parts && g_strv_length(parts) == 2) {
      width = atoi(parts[0]);
      height = atoi(parts[1]);

      plugin->mesh_width = width;
      plugin->mesh_height = height;

      g_strfreev(parts);
    }
  } break;
  case PROP_ASPECT_CORRECTION:
    plugin->aspect_correction = g_value_get_boolean(value);
    break;
  case PROP_EASTER_EGG:
    plugin->easter_egg = g_value_get_float(value);
    break;
  case PROP_PRESET_LOCKED:
    plugin->preset_locked = g_value_get_boolean(value);
    break;
  case PROP_ENABLE_PLAYLIST:
    plugin->enable_playlist = g_value_get_boolean(value);
    break;
  case PROP_SHUFFLE_PRESETS:
    plugin->shuffle_presets = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_projectm_get_property(GObject *object, guint property_id,
                               GValue *value, GParamSpec *pspec) {
  GstProjectM *plugin = GST_PROJECTM(object);

  const gchar *property_name = g_param_spec_get_name(pspec);
  GST_DEBUG_OBJECT(plugin, "get-property <%s>", property_name);

  switch (property_id) {
  case PROP_PRESET_PATH:
    g_value_set_string(value, plugin->preset_path);
    break;
  case PROP_TEXTURE_DIR_PATH:
    g_value_set_string(value, plugin->texture_dir_path);
    break;
  case PROP_BEAT_SENSITIVITY:
    g_value_set_float(value, plugin->beat_sensitivity);
    break;
  case PROP_HARD_CUT_DURATION:
    g_value_set_double(value, plugin->hard_cut_duration);
    break;
  case PROP_HARD_CUT_ENABLED:
    g_value_set_boolean(value, plugin->hard_cut_enabled);
    break;
  case PROP_HARD_CUT_SENSITIVITY:
    g_value_set_float(value, plugin->hard_cut_sensitivity);
    break;
  case PROP_SOFT_CUT_DURATION:
    g_value_set_double(value, plugin->soft_cut_duration);
    break;
  case PROP_PRESET_DURATION:
    g_value_set_double(value, plugin->preset_duration);
    break;
  case PROP_MESH_SIZE: {
    gchar *meshSizeStr =
        g_strdup_printf("%lu,%lu", plugin->mesh_width, plugin->mesh_height);
    g_value_set_string(value, meshSizeStr);
    g_free(meshSizeStr);
    break;
  }
  case PROP_ASPECT_CORRECTION:
    g_value_set_boolean(value, plugin->aspect_correction);
    break;
  case PROP_EASTER_EGG:
    g_value_set_float(value, plugin->easter_egg);
    break;
  case PROP_PRESET_LOCKED:
    g_value_set_boolean(value, plugin->preset_locked);
    break;
  case PROP_ENABLE_PLAYLIST:
    g_value_set_boolean(value, plugin->enable_playlist);
    break;
  case PROP_SHUFFLE_PRESETS:
    g_value_set_boolean(value, plugin->shuffle_presets);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void gst_projectm_init(GstProjectM *plugin) {
  plugin->priv = gst_projectm_get_instance_private(plugin);

  // Set default values for properties
  plugin->preset_path = DEFAULT_PRESET_PATH;
  plugin->texture_dir_path = DEFAULT_TEXTURE_DIR_PATH;
  plugin->beat_sensitivity = DEFAULT_BEAT_SENSITIVITY;
  plugin->hard_cut_duration = DEFAULT_HARD_CUT_DURATION;
  plugin->hard_cut_enabled = DEFAULT_HARD_CUT_ENABLED;
  plugin->hard_cut_sensitivity = DEFAULT_HARD_CUT_SENSITIVITY;
  plugin->soft_cut_duration = DEFAULT_SOFT_CUT_DURATION;
  plugin->preset_duration = DEFAULT_PRESET_DURATION;
  plugin->enable_playlist = DEFAULT_ENABLE_PLAYLIST;
  plugin->shuffle_presets = DEFAULT_SHUFFLE_PRESETS;

  const gchar *meshSizeStr = DEFAULT_MESH_SIZE;
  gint width, height;

  gchar **parts = g_strsplit(meshSizeStr, ",", 2);

  if (parts && g_strv_length(parts) == 2) {
    width = atoi(parts[0]);
    height = atoi(parts[1]);

    plugin->mesh_width = width;
    plugin->mesh_height = height;

    g_strfreev(parts);
  }

  plugin->aspect_correction = DEFAULT_ASPECT_CORRECTION;
  plugin->easter_egg = DEFAULT_EASTER_EGG;
  plugin->preset_locked = DEFAULT_PRESET_LOCKED;
  plugin->priv->handle = NULL;
  plugin->priv->fbo = NULL;
  plugin->priv->textureID = 0;
  plugin->priv->in_audio = NULL;
  plugin->priv->mem = NULL;
  plugin->priv->allocation_params = NULL;
}

static void gst_projectm_finalize(GObject *object) {
  GstProjectM *plugin = GST_PROJECTM(object);
  g_free(plugin->preset_path);
  g_free(plugin->texture_dir_path);
  G_OBJECT_CLASS(gst_projectm_parent_class)->finalize(object);
}

static void gst_projectm_gl_stop(GstGLBaseAudioVisualizer *src) {
  GstProjectM *plugin = GST_PROJECTM(src);
  if (plugin->priv->handle) {
    GST_DEBUG_OBJECT(plugin, "Destroying ProjectM instance");
    projectm_destroy(plugin->priv->handle);
    plugin->priv->handle = NULL;
  }
  if (plugin->priv->fbo) {
    gst_object_unref(plugin->priv->fbo);
    plugin->priv->fbo = NULL;
  }

  if (plugin->priv->textureID) {
    glDeleteTextures(1, &plugin->priv->textureID);
    plugin->priv->textureID = 0;
  }

  if (plugin->priv->allocation_params) {
    gst_gl_allocation_params_free(plugin->priv->allocation_params);
    plugin->priv->allocation_params = NULL;
  }
}

static gboolean gst_projectm_gl_start(GstGLBaseAudioVisualizer *glav) {
  // Cast the audio visualizer to the ProjectM plugin
  GstProjectM *plugin = GST_PROJECTM(glav);
  GstPMAudioVisualizer *gstav = GST_PM_AUDIO_VISUALIZER(glav);

#ifdef USE_GLEW
  GST_DEBUG_OBJECT(plugin, "Initializing GLEW");
  GLenum err = glewInit();
  if (GLEW_OK != err) {
    GST_ERROR_OBJECT(plugin, "GLEW initialization failed");
    return FALSE;
  }
#endif

  glGenTextures(1, &plugin->priv->textureID);
  glBindTexture(GL_TEXTURE_2D, plugin->priv->textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GST_VIDEO_INFO_WIDTH(&gstav->vinfo),
               GST_VIDEO_INFO_HEIGHT(&gstav->vinfo), 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  // glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, GST_VIDEO_INFO_WIDTH
  // (&gstav->vinfo), GST_VIDEO_INFO_HEIGHT (&gstav->vinfo)); glTexSubImage2D
  // (GL_TEXTURE_2D, 0, 0, 0, GST_VIDEO_INFO_WIDTH (&gstav->vinfo),
  // GST_VIDEO_INFO_HEIGHT (&gstav->vinfo), GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glBindTexture(GL_TEXTURE_2D, 0);

  plugin->priv->allocation_params =
      gst_gl_video_allocation_params_new_wrapped_texture(
          glav->context, NULL, &gstav->vinfo, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
          GST_GL_RGBA, plugin->priv->textureID, NULL, 0);

  // Check if ProjectM instance exists, and create if not
  if (!plugin->priv->handle) {
    // Create ProjectM instance
    plugin->priv->handle = projectm_init(plugin);
    if (!plugin->priv->handle) {
      GST_ERROR_OBJECT(plugin, "ProjectM could not be initialized");
      return FALSE;
    }
    gl_error_handler(glav->context, plugin);
  }

  plugin->priv->fbo = gst_gl_framebuffer_new_with_default_depth(
      glav->context, GST_VIDEO_INFO_WIDTH(&gstav->vinfo),
      GST_VIDEO_INFO_HEIGHT(&gstav->vinfo));

  /*
  glBindFramebuffer (GL_FRAMEBUFFER, plugin->priv->fbo->fbo_id);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
*/
  /*
   * Color Texture.
   *
   * IMPORTANT: create a *complete* texture with only one mipmap level.
   */
  // glGenTextures (1, &plugin->priv->textureID);
  // glBindTexture (GL_TEXTURE_2D, plugin->priv->textureID);
  // glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGB8, GST_VIDEO_INFO_WIDTH
  // (&gstav->vinfo), GST_VIDEO_INFO_HEIGHT (&gstav->vinfo)); glTexSubImage2D
  // (GL_TEXTURE_2D, 0, 0, 0, GST_VIDEO_INFO_WIDTH (&gstav->vinfo),
  // GST_VIDEO_INFO_HEIGHT (&gstav->vinfo), GL_RGB,
  //                  GL_UNSIGNED_BYTE, NULL);
  // glBindTexture (GL_TEXTURE_2D, 0);

  /*
   * Attach empty texture to framebuffer object: drawing to scene_fbo will use
   * scene_texture as the backing storage.
   */
  // glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
  // GL_TEXTURE_2D,
  //                          plugin->priv->textureID, 0);
  //
  //  glReadBuffer (GL_COLOR_ATTACHMENT0);

  //  GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
  //  glDrawBuffers (1, DrawBuffers);

  /* Sanity check. */
  //  if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
  //  {
  //    g_error("glCheckFramebufferStatus() failed.\n");
  //  }

  //  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  GST_INFO_OBJECT(plugin, "GL start complete");
  return TRUE;
}

static gboolean gst_projectm_setup(GstGLBaseAudioVisualizer *glav) {
  GstPMAudioVisualizer *bscope = GST_PM_AUDIO_VISUALIZER(glav);
  GstProjectM *plugin = GST_PROJECTM(glav);

  // Calculate depth based on pixel stride and bits
  gint depth = bscope->vinfo.finfo->pixel_stride[0] *
               ((bscope->vinfo.finfo->bits >= 8) ? 8 : 1);

  // Calculate required samples per frame
  bscope->req_spf =
      (bscope->ainfo.channels * bscope->ainfo.rate * 2) / bscope->vinfo.fps_n;

  // get GStreamer video format and map it to the corresponding OpenGL pixel
  // format
  const GstVideoFormat video_format = GST_VIDEO_INFO_FORMAT(&bscope->vinfo);

  // Log audio info
  GST_DEBUG_OBJECT(
      glav, "Audio Information <Channels: %d, SampleRate: %d, Description: %s>",
      bscope->ainfo.channels, bscope->ainfo.rate,
      bscope->ainfo.finfo->description);

  // Log video info
  GST_DEBUG_OBJECT(glav,
                   "Video Information <Dimensions: %dx%d, FPS: %d/%d, Depth: "
                   "%dbit, SamplesPerFrame: %d>",
                   GST_VIDEO_INFO_WIDTH(&bscope->vinfo),
                   GST_VIDEO_INFO_HEIGHT(&bscope->vinfo), bscope->vinfo.fps_n,
                   bscope->vinfo.fps_d, depth, bscope->req_spf);

  return TRUE;
}

// TODO: CLEANUP & ADD DEBUGGING
static gboolean gst_projectm_fill_gl_memory_callback(gpointer stuff) {
  GstProjectM *plugin = GST_PROJECTM(stuff);
  GstGLBaseAudioVisualizer *gstav = GST_GL_BASE_AUDIO_VISUALIZER(stuff);

  GstMapInfo audioMap;
  gboolean result = TRUE;

  // get current gst (PTS) time and set projectM time
  gdouble elapsed_seconds = (gdouble)gstav->pts / GST_SECOND;
  projectm_set_frame_time(plugin->priv->handle, elapsed_seconds);

  // AUDIO
  gst_buffer_map(plugin->priv->in_audio, &audioMap, GST_MAP_READ);

  // GST_DEBUG_OBJECT(plugin, "Audio Samples: %u, Offset: %lu, Offset End: %lu,
  // Sample Rate: %d, FPS: %d, Required Samples Per Frame: %d",
  //                  audioMap.size / 8, audio->offset, audio->offset_end,
  //                  bscope->ainfo.rate, bscope->vinfo.fps_n, bscope->req_spf);

  projectm_pcm_add_int16(plugin->priv->handle, (gint16 *)audioMap.data,
                         audioMap.size / 4, PROJECTM_STEREO);

  // GST_DEBUG_OBJECT(plugin, "Audio Data: %d %d %d %d", ((gint16
  // *)audioMap.data)[100], ((gint16 *)audioMap.data)[101], ((gint16
  // *)audioMap.data)[102], ((gint16 *)audioMap.data)[103]);

  // VIDEO
  GST_TRACE_OBJECT(plugin, "rendering projectM to fbo %d",
                   plugin->priv->fbo->fbo_id);
  projectm_opengl_render_frame_fbo(plugin->priv->handle,
                                   plugin->priv->fbo->fbo_id);

  gl_error_handler(gstav->context, plugin);

  gst_buffer_unmap(plugin->priv->in_audio, &audioMap);

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

  plugin->priv->in_audio = in_audio;
  plugin->priv->mem = mem;

  gboolean result = gst_gl_framebuffer_draw_to_texture(
      plugin->priv->fbo, mem, gst_projectm_fill_gl_memory_callback, plugin);

  plugin->priv->in_audio = NULL;
  plugin->priv->mem = NULL;

  return result;
}

static void gst_projectm_class_init(GstProjectMClass *klass) {
  GObjectClass *gobject_class = (GObjectClass *)klass;
  GstElementClass *element_class = (GstElementClass *)klass;
  GstGLBaseAudioVisualizerClass *scope_class =
      GST_GL_BASE_AUDIO_VISUALIZER_CLASS(klass);

  // Setup audio and video caps
  const gchar *audio_sink_caps = get_audio_sink_cap(0);
  const gchar *video_src_caps = get_video_src_cap(0);

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

  g_object_class_install_property(
      gobject_class, PROP_PRESET_PATH,
      g_param_spec_string(
          "preset", "Preset",
          "Specifies the path to the preset file. The preset file determines "
          "the visual style and behavior of the audio visualizer.",
          DEFAULT_PRESET_PATH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_TEXTURE_DIR_PATH,
      g_param_spec_string("texture-dir", "Texture Directory",
                          "Sets the path to the directory containing textures "
                          "used in the visualizer.",
                          DEFAULT_TEXTURE_DIR_PATH,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_BEAT_SENSITIVITY,
      g_param_spec_float(
          "beat-sensitivity", "Beat Sensitivity",
          "Controls the sensitivity to audio beats. Higher values make the "
          "visualizer respond more strongly to beats.",
          0.0, 5.0, DEFAULT_BEAT_SENSITIVITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_HARD_CUT_DURATION,
      g_param_spec_double("hard-cut-duration", "Hard Cut Duration",
                          "Sets the duration, in seconds, for hard cuts. Hard "
                          "cuts are abrupt transitions in the visualizer.",
                          0.0, 999999.0, DEFAULT_HARD_CUT_DURATION,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_HARD_CUT_ENABLED,
      g_param_spec_boolean(
          "hard-cut-enabled", "Hard Cut Enabled",
          "Enables or disables hard cuts. When enabled, the visualizer may "
          "exhibit sudden transitions based on the audio input.",
          DEFAULT_HARD_CUT_ENABLED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_HARD_CUT_SENSITIVITY,
      g_param_spec_float(
          "hard-cut-sensitivity", "Hard Cut Sensitivity",
          "Adjusts the sensitivity of the visualizer to hard cuts. Higher "
          "values increase the responsiveness to abrupt changes in audio.",
          0.0, 1.0, DEFAULT_HARD_CUT_SENSITIVITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SOFT_CUT_DURATION,
      g_param_spec_double(
          "soft-cut-duration", "Soft Cut Duration",
          "Sets the duration, in seconds, for soft cuts. Soft cuts are "
          "smoother transitions between visualizer states.",
          0.0, 999999.0, DEFAULT_SOFT_CUT_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_PRESET_DURATION,
      g_param_spec_double("preset-duration", "Preset Duration",
                          "Sets the duration, in seconds, for each preset. A "
                          "zero value causes the preset to play indefinitely.",
                          0.0, 999999.0, DEFAULT_PRESET_DURATION,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_MESH_SIZE,
      g_param_spec_string("mesh-size", "Mesh Size",
                          "Sets the size of the mesh used in rendering. The "
                          "format is 'width,height'.",
                          DEFAULT_MESH_SIZE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_ASPECT_CORRECTION,
      g_param_spec_boolean(
          "aspect-correction", "Aspect Correction",
          "Enables or disables aspect ratio correction. When enabled, the "
          "visualizer adjusts for aspect ratio differences in rendering.",
          DEFAULT_ASPECT_CORRECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_EASTER_EGG,
      g_param_spec_float(
          "easter-egg", "Easter Egg",
          "Controls the activation of an Easter Egg feature. The value "
          "determines the likelihood of triggering the Easter Egg.",
          0.0, 1.0, DEFAULT_EASTER_EGG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_PRESET_LOCKED,
      g_param_spec_boolean(
          "preset-locked", "Preset Locked",
          "Locks or unlocks the current preset. When locked, the visualizer "
          "remains on the current preset without automatic changes.",
          DEFAULT_PRESET_LOCKED,G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_ENABLE_PLAYLIST,
      g_param_spec_boolean(
          "enable-playlist", "Enable Playlist",
          "Enables or disables the playlist feature. When enabled, the "
          "visualizer can switch between presets based on a provided playlist.",
          DEFAULT_ENABLE_PLAYLIST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SHUFFLE_PRESETS,
      g_param_spec_boolean(
          "shuffle-presets", "Shuffle Presets",
          "Enables or disables preset shuffling. When enabled, the visualizer "
          "randomly selects presets from the playlist if presets are provided "
          "and not locked. Playlist must be enabled for this to take effect.",
          DEFAULT_SHUFFLE_PRESETS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_projectm_finalize;

  scope_class->supported_gl_api = GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
  scope_class->gl_start = GST_DEBUG_FUNCPTR(gst_projectm_gl_start);
  scope_class->gl_stop = GST_DEBUG_FUNCPTR(gst_projectm_gl_stop);
  scope_class->fill_gl_memory = GST_DEBUG_FUNCPTR(gst_projectm_fill_gl_memory);
  scope_class->setup = GST_DEBUG_FUNCPTR(gst_projectm_setup);
  scope_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR(gst_projectm_prepare_output_buffer);
}

static gboolean plugin_init(GstPlugin *plugin) {
  GST_DEBUG_CATEGORY_INIT(gst_projectm_debug, "projectm", 0,
                          "projectM visualizer plugin");

  return gst_element_register(plugin, "projectm", GST_RANK_NONE,
                              GST_TYPE_PROJECTM);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, projectm,
                  "plugin to visualize audio using the ProjectM library",
                  plugin_init, PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_NAME,
                  PACKAGE_ORIGIN)
