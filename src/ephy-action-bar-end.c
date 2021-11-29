/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2018 Purism SPC
 *  Copyright © 2018 Adrien Plazas <kekun.plazas@laposte.net>
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ephy-action-bar-end.h"
#include "ephy-add-bookmark-popover.h"
#include "ephy-desktop-utils.h"
#include "ephy-downloads-paintable.h"
#include "ephy-downloads-popover.h"
#include "ephy-shell.h"
#include "ephy-window.h"

#define NEEDS_ATTENTION_ANIMATION_TIMEOUT 2000 /*ms */

struct _EphyActionBarEnd {
  GtkBox parent_instance;

  GtkWidget *bookmark_button;
  GtkWidget *bookmarks_button;
  GtkWidget *downloads_revealer;
  GtkWidget *downloads_button;
  GtkWidget *downloads_popover;
  GtkWidget *downloads_icon;
  GtkWidget *browser_action_box;

  GdkPaintable *downloads_paintable;

  guint downloads_button_attention_timeout_id;
};

G_DEFINE_TYPE (EphyActionBarEnd, ephy_action_bar_end, GTK_TYPE_BOX)

static gboolean
add_attention_timeout_cb (EphyActionBarEnd *self)
{
  gtk_widget_remove_css_class (self->downloads_icon, "accent");
  self->downloads_button_attention_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
add_attention (EphyActionBarEnd *self)
{
  g_clear_handle_id (&self->downloads_button_attention_timeout_id, g_source_remove);

  gtk_widget_add_css_class (self->downloads_icon, "accent");
  self->downloads_button_attention_timeout_id = g_timeout_add (NEEDS_ATTENTION_ANIMATION_TIMEOUT,
                                                               G_SOURCE_FUNC (add_attention_timeout_cb),
                                                               self);
}

static void
download_added_cb (EphyDownloadsManager *manager,
                   EphyDownload         *download,
                   EphyActionBarEnd     *action_bar_end)
{
  if (!action_bar_end->downloads_popover) {
    action_bar_end->downloads_popover = ephy_downloads_popover_new ();
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (action_bar_end->downloads_button),
                                 action_bar_end->downloads_popover);
  }

  add_attention (action_bar_end);
  gtk_revealer_set_reveal_child (GTK_REVEALER (action_bar_end->downloads_revealer), TRUE);
}

static void
download_completed_cb (EphyDownloadsManager *manager,
                       EphyDownload         *download,
                       EphyActionBarEnd     *action_bar_end)
{
  ephy_downloads_paintable_animate_done (EPHY_DOWNLOADS_PAINTABLE (action_bar_end->downloads_paintable));
}

static void
download_removed_cb (EphyDownloadsManager *manager,
                     EphyDownload         *download,
                     EphyActionBarEnd     *action_bar_end)
{
  if (!ephy_downloads_manager_get_downloads (manager))
    gtk_revealer_set_reveal_child (GTK_REVEALER (action_bar_end->downloads_revealer), FALSE);
}

static void
downloads_estimated_progress_cb (EphyDownloadsManager *manager,
                                 EphyActionBarEnd     *action_bar_end)
{
  gdouble fraction = ephy_downloads_manager_get_estimated_progress (manager);

  g_object_set (action_bar_end->downloads_paintable, "progress", fraction, NULL);
}

static void
show_downloads_cb (EphyDownloadsManager *manager,
                   EphyActionBarEnd     *action_bar_end)
{
  if (gtk_widget_get_mapped (GTK_WIDGET (action_bar_end)))
    gtk_menu_button_popup (GTK_MENU_BUTTON (action_bar_end->downloads_button));
}

static void
ephy_action_bar_end_class_init (EphyActionBarEndClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/epiphany/gtk/action-bar-end.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        bookmark_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        bookmarks_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        downloads_revealer);
  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        downloads_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        downloads_icon);
  gtk_widget_class_bind_template_child (widget_class,
                                        EphyActionBarEnd,
                                        browser_action_box);
}

static void
ephy_action_bar_end_init (EphyActionBarEnd *action_bar_end)
{
  GObject *object = G_OBJECT (action_bar_end);
  EphyDownloadsManager *downloads_manager;
  GtkWidget *popover;

  gtk_widget_init_template (GTK_WIDGET (action_bar_end));

  /* Downloads */
  downloads_manager = ephy_embed_shell_get_downloads_manager (ephy_embed_shell_get_default ());

  gtk_revealer_set_reveal_child (GTK_REVEALER (action_bar_end->downloads_revealer),
                                 ephy_downloads_manager_get_downloads (downloads_manager) != NULL);

  if (ephy_downloads_manager_get_downloads (downloads_manager)) {
    action_bar_end->downloads_popover = ephy_downloads_popover_new ();
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (action_bar_end->downloads_button), action_bar_end->downloads_popover);
  }

  action_bar_end->downloads_paintable = ephy_downloads_paintable_new (action_bar_end->downloads_icon);
  gtk_image_set_from_paintable (GTK_IMAGE (action_bar_end->downloads_icon),
                                action_bar_end->downloads_paintable);

  if (is_desktop_pantheon ()) {
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (action_bar_end->bookmarks_button),
                                   "user-bookmarks");
  }

  g_signal_connect_object (downloads_manager, "download-added",
                           G_CALLBACK (download_added_cb),
                           object, 0);
  g_signal_connect_object (downloads_manager, "download-completed",
                           G_CALLBACK (download_completed_cb),
                           object, 0);
  g_signal_connect_object (downloads_manager, "download-removed",
                           G_CALLBACK (download_removed_cb),
                           object, 0);
  g_signal_connect_object (downloads_manager, "estimated-progress-changed",
                           G_CALLBACK (downloads_estimated_progress_cb),
                           object, 0);
  g_signal_connect_object (downloads_manager, "show-downloads",
                           G_CALLBACK (show_downloads_cb),
                           object, 0);

  popover = ephy_add_bookmark_popover_new ();

  gtk_menu_button_set_popover (GTK_MENU_BUTTON (action_bar_end->bookmark_button), popover);
}

EphyActionBarEnd *
ephy_action_bar_end_new (void)
{
  return g_object_new (EPHY_TYPE_ACTION_BAR_END,
                       NULL);
}

void
ephy_action_bar_end_set_show_bookmarks_button (EphyActionBarEnd *action_bar_end,
                                               gboolean          show)
{
  gtk_widget_set_visible (action_bar_end->bookmarks_button, show);
}

void
ephy_action_bar_end_show_downloads (EphyActionBarEnd *action_bar_end)
{
  if (gtk_widget_get_visible (action_bar_end->downloads_button))
    gtk_menu_button_popup (GTK_MENU_BUTTON (action_bar_end->downloads_button));
}

void
ephy_action_bar_end_show_bookmarks (EphyActionBarEnd *action_bar_end)
{
  if (gtk_widget_get_visible (action_bar_end->bookmarks_button))
    gtk_menu_button_popup (GTK_MENU_BUTTON (action_bar_end->bookmarks_button));
}

GtkWidget *
ephy_action_bar_end_get_bookmarks_button (EphyActionBarEnd *action_bar_end)
{
  return action_bar_end->bookmarks_button;
}

GtkWidget *
ephy_action_bar_end_get_downloads_revealer (EphyActionBarEnd *action_bar_end)
{
  return action_bar_end->downloads_revealer;
}

void
ephy_action_bar_end_add_browser_action (EphyActionBarEnd *action_bar_end,
                                        GtkWidget        *action)
{
  gtk_box_append (GTK_BOX (action_bar_end->browser_action_box), action);
}

void
ephy_action_bar_end_set_show_bookmark_button (EphyActionBarEnd *action_bar_end,
                                              gboolean          show)
{
  gtk_widget_set_visible (action_bar_end->bookmark_button, show);
}


void
ephy_action_bar_end_set_bookmark_icon_state (EphyActionBarEnd      *action_bar_end,
                                             EphyBookmarkIconState  state)
{
  g_assert (EPHY_IS_ACTION_BAR_END (action_bar_end));

  switch (state) {
    case EPHY_BOOKMARK_ICON_HIDDEN:
      gtk_widget_set_visible (action_bar_end->bookmark_button, FALSE);
      break;
    case EPHY_BOOKMARK_ICON_EMPTY:
      gtk_widget_set_visible (action_bar_end->bookmark_button, TRUE);
      gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (action_bar_end->bookmark_button),
                                     "non-starred-symbolic");
      break;
    case EPHY_BOOKMARK_ICON_BOOKMARKED:
      gtk_widget_set_visible (action_bar_end->bookmark_button, TRUE);
      gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (action_bar_end->bookmark_button),
                                     "starred-symbolic");
      break;
    default:
      g_assert_not_reached ();
  }
}
