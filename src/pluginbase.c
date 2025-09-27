
#include "pluginbase.h"

#include "enums.h"

#include "config.h"
// #endif
#include "debug.h"
#include "gstglbaseaudiovisualizer.h"

#include <gst/gl/gstglframebuffer.h>
#include <tgmath.h>

GST_DEBUG_CATEGORY_STATIC(gst_projectm_base_debug);
#define GST_CAT_DEFAULT gst_projectm_base_debug

void projectm_base_init_once() {
  GST_DEBUG_CATEGORY_INIT(gst_projectm_base_debug, "projectm_base", 0,
                          "projectM visualizer plugin base");
}

static gboolean projectm_init(GObject *plugin,
                              GstBaseProjectMSettings *settings,
                              GstVideoInfo *vinfo, projectm_handle *ret_handle,
                              projectm_playlist_handle *ret_playlist) {
  projectm_handle handle = NULL;
  projectm_playlist_handle playlist = NULL;

  // Create ProjectM instance
  GST_DEBUG_OBJECT(plugin, "Creating projectM instance..");
  handle = projectm_create();

  if (!handle) {
    GST_DEBUG_OBJECT(
        plugin,
        "project_create() returned NULL, projectM instance was not created!");
    return FALSE;
  } else {
    GST_DEBUG_OBJECT(plugin, "Created projectM instance!");
  }
  *ret_handle = handle;

  if (settings->enable_playlist) {
    GST_DEBUG_OBJECT(plugin, "Playlist enabled");

    // initialize preset playlist
    playlist = projectm_playlist_create(handle);
    *ret_playlist = playlist;
    projectm_playlist_set_shuffle(playlist, settings->shuffle_presets);
    // projectm_playlist_set_preset_switched_event_callback(_playlist,
    // &ProjectMWrapper::PresetSwitchedEvent, static_cast<void*>(this));
  } else {
    GST_DEBUG_OBJECT(plugin, "Playlist disabled");
  }
  // Log properties
  GST_INFO_OBJECT(plugin,
                  "Using Properties: "
                  "preset=%s, "
                  "texture-dir=%s, "
                  "beat-sensitivity=%f, "
                  "hard-cut-duration=%f, "
                  "hard-cut-enabled=%d, "
                  "hard-cut-sensitivity=%f, "
                  "soft-cut-duration=%f, "
                  "preset-duration=%f, "
                  "mesh-size=(%lu, %lu)"
                  "aspect-correction=%d, "
                  "easter-egg=%f, "
                  "preset-locked=%d, "
                  "enable-playlist=%d, "
                  "shuffle-presets=%d",
                  settings->preset_path, settings->texture_dir_path,
                  settings->beat_sensitivity, settings->hard_cut_duration,
                  settings->hard_cut_enabled, settings->hard_cut_sensitivity,
                  settings->soft_cut_duration, settings->preset_duration,
                  settings->mesh_width, settings->mesh_height,
                  settings->aspect_correction, settings->easter_egg,
                  settings->preset_locked, settings->enable_playlist,
                  settings->shuffle_presets);

  // Load preset file if path is provided
  if (settings->preset_path != NULL) {
    unsigned int added_count = projectm_playlist_add_path(
        playlist, settings->preset_path, true, false);
    GST_INFO_OBJECT(plugin, "Loaded preset path: %s, presets found: %d",
                    settings->preset_path, added_count);
  }

  // Set texture search path if directory path is provided
  if (settings->texture_dir_path != NULL) {
    const gchar *texturePaths[1] = {settings->texture_dir_path};
    projectm_set_texture_search_paths(handle, texturePaths, 1);
  }

  // Set properties
  projectm_set_beat_sensitivity(handle, settings->beat_sensitivity);
  projectm_set_hard_cut_duration(handle, settings->hard_cut_duration);
  projectm_set_hard_cut_enabled(handle, settings->hard_cut_enabled);
  projectm_set_hard_cut_sensitivity(handle, settings->hard_cut_sensitivity);
  projectm_set_soft_cut_duration(handle, settings->soft_cut_duration);

  // Set preset duration, or set to in infinite duration if zero
  if (settings->preset_duration > 0.0) {
    projectm_set_preset_duration(handle, settings->preset_duration);
    // kick off the first preset
    if (projectm_playlist_size(playlist) > 1 && !settings->preset_locked) {
      projectm_playlist_play_next(playlist, true);
    }
  } else {
    projectm_set_preset_duration(handle, 999999.0);
  }

  projectm_set_mesh_size(handle, settings->mesh_width, settings->mesh_height);
  projectm_set_aspect_correction(handle, settings->aspect_correction);
  projectm_set_easter_egg(handle, settings->easter_egg);
  projectm_set_preset_locked(handle, settings->preset_locked);

  gdouble fps;
  gst_util_fraction_to_double(GST_VIDEO_INFO_FPS_N(vinfo),
                              GST_VIDEO_INFO_FPS_D(vinfo), &fps);

  projectm_set_fps(handle, gst_util_gdouble_to_guint64(fps));
  projectm_set_window_size(handle, GST_VIDEO_INFO_WIDTH(vinfo),
                           GST_VIDEO_INFO_HEIGHT(vinfo));

  return TRUE;
}

void gst_projectm_base_set_property(GObject *object,
                                    GstBaseProjectMSettings *settings,
                                    guint property_id, const GValue *value,
                                    GParamSpec *pspec) {

  const gchar *property_name = g_param_spec_get_name(pspec);
  GST_DEBUG_OBJECT(object, "set-property <%s>", property_name);

  switch (property_id) {
  case PROP_PRESET_PATH:
    settings->preset_path = g_strdup(g_value_get_string(value));
    break;
  case PROP_TEXTURE_DIR_PATH:
    settings->texture_dir_path = g_strdup(g_value_get_string(value));
    break;
  case PROP_BEAT_SENSITIVITY:
    settings->beat_sensitivity = g_value_get_float(value);
    break;
  case PROP_HARD_CUT_DURATION:
    settings->hard_cut_duration = g_value_get_double(value);
    break;
  case PROP_HARD_CUT_ENABLED:
    settings->hard_cut_enabled = g_value_get_boolean(value);
    break;
  case PROP_HARD_CUT_SENSITIVITY:
    settings->hard_cut_sensitivity = g_value_get_float(value);
    break;
  case PROP_SOFT_CUT_DURATION:
    settings->soft_cut_duration = g_value_get_double(value);
    break;
  case PROP_PRESET_DURATION:
    settings->preset_duration = g_value_get_double(value);
    break;
  case PROP_MESH_SIZE: {
    const gchar *meshSizeStr = g_value_get_string(value);
    gint width, height;

    gchar **parts = g_strsplit(meshSizeStr, ",", 2);

    if (parts && g_strv_length(parts) == 2) {
      width = atoi(parts[0]);
      height = atoi(parts[1]);

      settings->mesh_width = width;
      settings->mesh_height = height;

      g_strfreev(parts);
    }
  } break;
  case PROP_ASPECT_CORRECTION:
    settings->aspect_correction = g_value_get_boolean(value);
    break;
  case PROP_EASTER_EGG:
    settings->easter_egg = g_value_get_float(value);
    break;
  case PROP_PRESET_LOCKED:
    settings->preset_locked = g_value_get_boolean(value);
    break;
  case PROP_ENABLE_PLAYLIST:
    settings->enable_playlist = g_value_get_boolean(value);
    break;
  case PROP_SHUFFLE_PRESETS:
    settings->shuffle_presets = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_projectm_base_get_property(GObject *object,
                                    GstBaseProjectMSettings *settings,
                                    guint property_id, GValue *value,
                                    GParamSpec *pspec) {

  const gchar *property_name = g_param_spec_get_name(pspec);
  GST_DEBUG_OBJECT(settings, "get-property <%s>", property_name);

  switch (property_id) {
  case PROP_PRESET_PATH:
    g_value_set_string(value, settings->preset_path);
    break;
  case PROP_TEXTURE_DIR_PATH:
    g_value_set_string(value, settings->texture_dir_path);
    break;
  case PROP_BEAT_SENSITIVITY:
    g_value_set_float(value, settings->beat_sensitivity);
    break;
  case PROP_HARD_CUT_DURATION:
    g_value_set_double(value, settings->hard_cut_duration);
    break;
  case PROP_HARD_CUT_ENABLED:
    g_value_set_boolean(value, settings->hard_cut_enabled);
    break;
  case PROP_HARD_CUT_SENSITIVITY:
    g_value_set_float(value, settings->hard_cut_sensitivity);
    break;
  case PROP_SOFT_CUT_DURATION:
    g_value_set_double(value, settings->soft_cut_duration);
    break;
  case PROP_PRESET_DURATION:
    g_value_set_double(value, settings->preset_duration);
    break;
  case PROP_MESH_SIZE: {
    gchar *meshSizeStr =
        g_strdup_printf("%lu,%lu", settings->mesh_width, settings->mesh_height);
    g_value_set_string(value, meshSizeStr);
    g_free(meshSizeStr);
    break;
  }
  case PROP_ASPECT_CORRECTION:
    g_value_set_boolean(value, settings->aspect_correction);
    break;
  case PROP_EASTER_EGG:
    g_value_set_float(value, settings->easter_egg);
    break;
  case PROP_PRESET_LOCKED:
    g_value_set_boolean(value, settings->preset_locked);
    break;
  case PROP_ENABLE_PLAYLIST:
    g_value_set_boolean(value, settings->enable_playlist);
    break;
  case PROP_SHUFFLE_PRESETS:
    g_value_set_boolean(value, settings->shuffle_presets);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_projectm_base_init(GstBaseProjectMSettings *settings,
                            GstBaseProjectMPrivate *priv) {

  // Set default values for properties
  settings->preset_path = DEFAULT_PRESET_PATH;
  settings->texture_dir_path = DEFAULT_TEXTURE_DIR_PATH;
  settings->beat_sensitivity = DEFAULT_BEAT_SENSITIVITY;
  settings->hard_cut_duration = DEFAULT_HARD_CUT_DURATION;
  settings->hard_cut_enabled = DEFAULT_HARD_CUT_ENABLED;
  settings->hard_cut_sensitivity = DEFAULT_HARD_CUT_SENSITIVITY;
  settings->soft_cut_duration = DEFAULT_SOFT_CUT_DURATION;
  settings->preset_duration = DEFAULT_PRESET_DURATION;
  settings->enable_playlist = DEFAULT_ENABLE_PLAYLIST;
  settings->shuffle_presets = DEFAULT_SHUFFLE_PRESETS;

  const gchar *meshSizeStr = DEFAULT_MESH_SIZE;
  gint width, height;

  gchar **parts = g_strsplit(meshSizeStr, ",", 2);

  if (parts && g_strv_length(parts) == 2) {
    width = atoi(parts[0]);
    height = atoi(parts[1]);

    settings->mesh_width = width;
    settings->mesh_height = height;

    g_strfreev(parts);
  }

  settings->aspect_correction = DEFAULT_ASPECT_CORRECTION;
  settings->easter_egg = DEFAULT_EASTER_EGG;
  settings->preset_locked = DEFAULT_PRESET_LOCKED;

  priv->first_frame_time = 0;
  priv->first_frame_received = FALSE;

  g_mutex_init(&priv->projectm_lock);
}

void gst_projectm_base_finalize(GstBaseProjectMSettings *settings,
                                GstBaseProjectMPrivate *priv) {
  g_free(settings->preset_path);
  g_free(settings->texture_dir_path);
  g_mutex_clear(&priv->projectm_lock);
}

gboolean gst_projectm_base_gl_start(GObject *plugin,
                                    GstBaseProjectMPrivate *priv,
                                    GstBaseProjectMSettings *settings,
                                    GstGLContext *context,
                                    GstVideoInfo *vinfo) {

#ifdef USE_GLEW
  GST_DEBUG_OBJECT(plugin, "Initializing GLEW");
  GLenum err = glewInit();
  if (GLEW_OK != err) {
    GST_ERROR_OBJECT(plugin, "GLEW initialization failed");
    return FALSE;
  }
#endif

  // initialize render texture
  priv->fbo = gst_gl_framebuffer_new_with_default_depth(
      context, GST_VIDEO_INFO_WIDTH(vinfo), GST_VIDEO_INFO_HEIGHT(vinfo));

  // Check if ProjectM instance exists, and create if not
  if (!priv->handle) {
    // Create ProjectM instance
    priv->first_frame_received = FALSE;
    if (!projectm_init(plugin, settings, vinfo, &priv->handle,
                       &priv->playlist)) {
      GST_ERROR_OBJECT(plugin, "projectM could not be initialized");
      return FALSE;
    }
    gl_error_handler(context);
  }

  GST_INFO_OBJECT(plugin, "projectM GL start complete");
  return TRUE;
}

void gst_projectm_base_gl_stop(GObject *plugin, GstBaseProjectMPrivate *priv) {

  if (priv->handle) {
    GST_DEBUG_OBJECT(plugin, "Destroying ProjectM instance");
    projectm_destroy(priv->handle);
    priv->handle = NULL;
  }
  if (priv->fbo) {
    gst_object_unref(priv->fbo);
    priv->fbo = NULL;
  }
}

gdouble get_seconds_since_first_frame(GstBaseProjectMPrivate *priv,
                                      GstClockTime pts) {
  // timestamp to sync to
  GstClockTime current_time = pts;

  if (!priv->first_frame_received) {
    // Store the timestamp of the first frame
    priv->first_frame_time = current_time;
    priv->first_frame_received = TRUE;
    return 0.0;
  }

  // Calculate elapsed time
  GstClockTime elapsed_time = current_time - priv->first_frame_time;

  // Convert to fractional seconds
  gdouble elapsed_seconds = (gdouble)elapsed_time / GST_SECOND;

  return elapsed_seconds;
}

void gst_projectm_base_fill_audio_buffer(GstBaseProjectMPrivate *priv,
                                         GstBuffer *in_audio) {

  if (in_audio != NULL) {

    GstMapInfo audioMap;

    gst_buffer_map(in_audio, &audioMap, GST_MAP_READ);

    projectm_pcm_add_int16(priv->handle, (gint16 *)audioMap.data,
                           audioMap.size / 4, PROJECTM_STEREO);

    gst_buffer_unmap(in_audio, &audioMap);
  }
}

void gst_projectm_base_fill_gl_memory_callback(
    GstObject *plugin, GstBaseProjectMPrivate *priv,
    GstBaseProjectMSettings *settings, GstGLContext *context, GstClockTime pts,
    GstBuffer *in_audio) {

  // get current gst sync time (pts) and set projectM time
  gdouble seconds_since_first_frame = get_seconds_since_first_frame(priv, pts);

  gdouble time = (gdouble)pts / GST_SECOND;
  if (fabs(time - seconds_since_first_frame) > 0.00001) {
    GST_DEBUG_OBJECT(plugin, "Injecting projectM timestamp %f s, pts %f ms",
                     seconds_since_first_frame, time);
  }

  projectm_set_frame_time(priv->handle, seconds_since_first_frame);

  // process audio buffer
  gst_projectm_base_fill_audio_buffer(priv, in_audio);

  // render the frame
  projectm_opengl_render_frame_fbo(priv->handle, priv->fbo->fbo_id);

  gl_error_handler(context);
}

void gst_projectm_base_install_properties(GObjectClass *gobject_class) {

  // Setup properties
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
}