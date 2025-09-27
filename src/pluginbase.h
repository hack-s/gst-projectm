
#ifndef PLUGINBASE_H
#define PLUGINBASE_H

#include <gst/gl/gstglmemory.h>
#include <gst/gst.h>
#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

/*
 * Basic gst/projectM integration structs and functions that can be re-used for
 * alternative plugin implementations.
 */
G_BEGIN_DECLS

/**
 * projectM config properties.
 */
struct _GstBaseProjectMSettings {

  gchar *preset_path;
  gchar *texture_dir_path;

  gfloat beat_sensitivity;
  gdouble hard_cut_duration;
  gboolean hard_cut_enabled;
  gfloat hard_cut_sensitivity;
  gdouble soft_cut_duration;
  gdouble preset_duration;
  gulong mesh_width;
  gulong mesh_height;
  gboolean aspect_correction;
  gfloat easter_egg;
  gboolean preset_locked;
  gboolean enable_playlist;
  gboolean shuffle_presets;
};

/**
 * Variables needed for projectM (fbo) rendering.
 */
struct _GstBaseProjectMPrivate {
  projectm_handle handle;
  projectm_playlist_handle playlist;
  GMutex projectm_lock;

  GstClockTime first_frame_time;
  gboolean first_frame_received;

  GstGLFramebuffer *fbo;
};

typedef struct _GstBaseProjectMPrivate GstBaseProjectMPrivate;
typedef struct _GstBaseProjectMSettings GstBaseProjectMSettings;

/**
 * One time initialization. Should be called once before any other function is
 * this unit.
 */
void projectm_base_init_once();

/**
 * get_property delegate for projectM setting structs.
 *
 * @param object Plugin gst object.
 * @param settings Settings struct to update.
 * @param property_id Property id to update.
 * @param value Property value.
 * @param pspec Gst param type spec.
 */
void gst_projectm_base_set_property(GObject *object,
                                    GstBaseProjectMSettings *settings,
                                    guint property_id, const GValue *value,
                                    GParamSpec *pspec);

/**
 * set_property delegate for projectM setting structs.
 *
 * @param object Plugin gst object.
 * @param settings Settings struct to update.
 * @param property_id Property id to update.
 * @param value Property value.
 * @param pspec Gst param type spec.
 */
void gst_projectm_base_get_property(GObject *object,
                                    GstBaseProjectMSettings *settings,
                                    guint property_id, GValue *value,
                                    GParamSpec *pspec);

/**
 * Plugin init() delegate for projectM settings and priv.
 *
 * @param settings Settings to init.
 * @param priv Private obj to init.
 */
void gst_projectm_base_init(GstBaseProjectMSettings *settings,
                            GstBaseProjectMPrivate *priv);

/**
 * Plugin finalize() delegate for projectM settings and priv.
 *
 * @param settings Settings to init.
 * @param priv Private obj to init.
 */
void gst_projectm_base_finalize(GstBaseProjectMSettings *settings,
                                GstBaseProjectMPrivate *priv);

/**
 * GL start delegate to setup projectM fbo rendering.
 *
 * @param plugin Plugin gst object.
 * @param priv Plugin priv data.
 * @param settings Plugin settings.
 * @param context The gl context to use for projectM rendering.
 * @param vinfo Video rendering details.
 *
 * @return TRUE on success.
 */
gboolean gst_projectm_base_gl_start(GObject *plugin,
                                    GstBaseProjectMPrivate *priv,
                                    GstBaseProjectMSettings *settings,
                                    GstGLContext *context, GstVideoInfo *vinfo);

/**
 * GL stop delegate to clean up projectM rendering resources.
 *
 * @param plugin Plugin gst object.
 * @param priv Plugin priv data.
 */
void gst_projectm_base_gl_stop(GObject *plugin, GstBaseProjectMPrivate *priv);

/**
 * Just pushes audio data to projectM without rendering.
 *
 * @param priv Plugin priv data.
 * @param in_audio Audio data buffer to push to projectM.
 */
void gst_projectm_base_fill_audio_buffer(GstBaseProjectMPrivate *priv,
                                         GstBuffer *in_audio);

/**
 * Render one frame with projectM.
 *
 * @param plugin Plugin gst object.
 * @param priv Plugin priv data.
 * @param settings Plugin settings.
 * @param context ProjectM GL context.
 * @param pts Current pts timestamp.
 * @param in_audio Input audio buffer to push to projectM before rendering, may
 * be NULL.
 */
void gst_projectm_base_fill_gl_memory_callback(
    GstObject *plugin, GstBaseProjectMPrivate *priv,
    GstBaseProjectMSettings *settings, GstGLContext *context, GstClockTime pts,
    GstBuffer *in_audio);

/**
 * Install properties from projectM settings to given plugin class.
 *
 * @param gobject_class Plugin class to install properties to.
 */
void gst_projectm_base_install_properties(GObjectClass *gobject_class);

#define GST_PROJECTM_BASE_LOCK(plugin)                                         \
  (g_mutex_lock(&plugin->priv->base.projectm_lock))
#define GST_PROJECTM_BASE_UNLOCK(plugin)                                       \
  (g_mutex_unlock(&plugin->priv->base.projectm_lock))

G_END_DECLS

#endif // PLUGINBASE_H
