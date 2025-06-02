#include <projectM-4/parameters.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_GLEW
#include <GL/glew.h>
#endif
#include <gst/gl/gstglfuncs.h>
#include <gst/gst.h>

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

  GstClockTime first_frame_time;
  gboolean first_frame_received;

  GstGLFramebuffer *fbo;
  GLuint texture_id;
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
  gpointer glTextures[1];
  GstGLFormat glFormats[1];
  GstBuffer *glBuffer;
  gboolean ret;

  allocator = gst_gl_memory_allocator_get_default(glav->context);

  glBuffer = gst_buffer_new();
  if (!glBuffer) {
    g_error("Failed to create new buffer\n");
    return NULL;
  }

  glTextures[0] = (gpointer)plugin->priv->texture_id;
  glFormats[0] = GST_GL_RGBA8;

  // create gl mem buffer for texture
  ret = gst_gl_memory_setup_buffer(allocator, glBuffer,
                                   plugin->priv->allocation_params, glFormats,
                                   glTextures, 1);
  if (!ret) {
    g_error("Failed to setup gl memory\n");
    return NULL;
  }

  gst_object_unref(allocator);

  return glBuffer;
}

static GstFlowReturn
gst_projectm_prepare_output_buffer(GstGLBaseAudioVisualizer *scope,
                                   GstBuffer **outbuf) {
  GstProjectM *plugin = GST_PROJECTM(scope);

  *outbuf = wrap_gl_texture(scope, plugin);
  GST_DEBUG_OBJECT(plugin, "Wrapped RT texture buffer");
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
  case PROP_PTS_SYNC:
    plugin->pts_sync = g_value_get_boolean(value);
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
  case PROP_PTS_SYNC:
    g_value_set_boolean(value, plugin->pts_sync);
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
  plugin->pts_sync = true;
  plugin->priv->first_frame_time = 0;
  plugin->priv->first_frame_received = FALSE;

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
  plugin->priv->texture_id = 0;
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

  if (plugin->priv->texture_id) {
    glDeleteTextures(1, &plugin->priv->texture_id);
    plugin->priv->texture_id = 0;
  }

  if (plugin->priv->allocation_params) {
    gst_gl_video_allocation_params_free_data(plugin->priv->allocation_params);
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

  // initialize render texture
  // todo: let gst create the texture
  const GstGLFuncs *glFunctions = glav->context->gl_vtable;

  glFunctions->GenTextures(1, &plugin->priv->texture_id);
  glFunctions->BindTexture(GL_TEXTURE_2D, plugin->priv->texture_id);

  // allocate texture
  glFunctions->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GST_VIDEO_INFO_WIDTH(&gstav->vinfo),
               GST_VIDEO_INFO_HEIGHT(&gstav->vinfo), 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);

  glFunctions->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFunctions->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glFunctions->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glFunctions->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glFunctions->BindTexture(GL_TEXTURE_2D, 0);

  plugin->priv->allocation_params =
      gst_gl_video_allocation_params_new_wrapped_texture(
          glav->context, NULL, &gstav->vinfo, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
          GST_GL_RGBA, plugin->priv->texture_id, NULL, 0);

  // Check if ProjectM instance exists, and create if not
  if (!plugin->priv->handle) {
    // Create ProjectM instance
    plugin->priv->handle = projectm_init(plugin);
    plugin->priv->first_frame_received = FALSE;
    if (!plugin->priv->handle) {
      GST_ERROR_OBJECT(plugin, "ProjectM could not be initialized");
      return FALSE;
    }
    gl_error_handler(glav->context, plugin);
  }

  plugin->priv->fbo = gst_gl_framebuffer_new_with_default_depth(
      glav->context, GST_VIDEO_INFO_WIDTH(&gstav->vinfo),
      GST_VIDEO_INFO_HEIGHT(&gstav->vinfo));

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

static gdouble get_seconds_since_first_frame(GstProjectM *plugin,
                                             GstGLBaseAudioVisualizer *glav) {
  // pick timestamp to sync to
  GstClockTime current_time;
  if (plugin->pts_sync) {
    // sync to pts
    current_time = glav->pts;
  } else {
    // sync to dts
    GstPMAudioVisualizer *pmav = GST_PM_AUDIO_VISUALIZER(plugin);
    current_time = pmav->stream_time;
  }

  if (!plugin->priv->first_frame_received) {
    // Store the timestamp of the first frame
    plugin->priv->first_frame_time = current_time;
    plugin->priv->first_frame_received = TRUE;
    return 0.0;
  }

  // Calculate elapsed time
  GstClockTime elapsed_time = current_time - plugin->priv->first_frame_time;

  // Convert to fractional seconds
  gdouble elapsed_seconds = (gdouble)elapsed_time / GST_SECOND;

  return elapsed_seconds;
}

static gboolean gst_projectm_fill_gl_memory_callback(gpointer stuff) {
  GstProjectM *plugin = GST_PROJECTM(stuff);
  GstGLBaseAudioVisualizer *glav = GST_GL_BASE_AUDIO_VISUALIZER(stuff);

  GstMapInfo audioMap;
  gboolean result = TRUE;

  // get current gst sync time (pts or stream time/dts) and set projectM time
  gdouble seconds_since_first_frame =
      get_seconds_since_first_frame(plugin, glav);

  projectm_set_frame_time(plugin->priv->handle, seconds_since_first_frame);

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

  gl_error_handler(glav->context, plugin);

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
          DEFAULT_PRESET_LOCKED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  g_object_class_install_property(
      gobject_class, PROP_PTS_SYNC,
      g_param_spec_boolean(
          "pts-sync", "Presentation Timestamp Sync",
          "If true, projectM will be synced to the gst presentation timestamp. "
          "In case of false, the stream time (dts) will be used.",
          DEFAULT_PTS_SYNC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
