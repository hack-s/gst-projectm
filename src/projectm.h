#ifndef __PROJECTM_H__
#define __PROJECTM_H__

#include <glib.h>

#include "plugin.h"
#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

G_BEGIN_DECLS

/**
 * @brief Initialize ProjectM
 */
bool projectm_init(GstProjectM *plugin, projectm_handle *handle,
                   projectm_playlist_handle *playlist);

/**
 * @brief Render ProjectM
 */
// void projectm_render(GstProjectM *plugin, gint16 *samples, gint
// sample_count);

G_END_DECLS

#endif /* __PROJECTM_H__ */