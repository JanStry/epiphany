/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2016 Iulian-Gabriel Radu <iulian.radu67@gmail.com>
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

#include "config.h"

#include "ephy-bookmarks-manager.h"

#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "gvdb-builder.h"
#include "gvdb-reader.h"

#define EPHY_BOOKMARKS_FILE "bookmarks.gvdb"

struct _EphyBookmarksManager {
  GObject     parent_instance;

  GSequence  *bookmarks;
  GSequence  *tags;

  gchar      *gvdb_filename;
};

G_DEFINE_TYPE (EphyBookmarksManager, ephy_bookmarks_manager, G_TYPE_OBJECT)

enum {
  BOOKMARK_ADDED,
  BOOKMARK_REMOVED,
  BOOKMARK_TITLE_CHANGED,
  BOOKMARK_URL_CHANGED,
  TAG_CREATED,
  TAG_DELETED,
  LAST_SIGNAL
};

static guint       signals[LAST_SIGNAL];

static void
gvdb_hash_table_insert_variant (GHashTable *table,
                                const char *key,
                                GVariant   *value)
{
  GvdbItem *item;

  item = gvdb_hash_table_insert (table, key);
  gvdb_item_set_value (item, value);
}

static GVariant *
build_variant (EphyBookmark *bookmark)
{
  GVariantBuilder builder;
  GSequence *tags;
  GSequenceIter *iter;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(xssdbas)"));

  g_variant_builder_add (&builder, "x", ephy_bookmark_get_time_added (bookmark));
  g_variant_builder_add (&builder, "s", ephy_bookmark_get_title (bookmark));
  g_variant_builder_add (&builder, "s", ephy_bookmark_get_id (bookmark));
  g_variant_builder_add (&builder, "d", ephy_bookmark_get_modification_time (bookmark));
  g_variant_builder_add (&builder, "b", ephy_bookmark_is_uploaded (bookmark));

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("as"));
  tags = ephy_bookmark_get_tags (bookmark);
  for (iter = g_sequence_get_begin_iter (tags);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    g_variant_builder_add (&builder, "s", g_sequence_get (iter));
  }
  g_variant_builder_close (&builder);

  return g_variant_builder_end (&builder);
}

static void
add_bookmark_to_table (EphyBookmark *bookmark, GHashTable *table)
{
  gvdb_hash_table_insert_variant (table,
                                  ephy_bookmark_get_url (bookmark),
                                  build_variant (bookmark));
}

static void
add_tag_to_table (const char *tag, GHashTable *table)
{
  gvdb_hash_table_insert (table, tag);
}

static void
ephy_bookmarks_manager_save_to_file (EphyBookmarksManager *self, GTask *task)
{
  GHashTable *root_table;
  GHashTable *table;
  gboolean result;

  root_table = gvdb_hash_table_new (NULL, NULL);

  table = gvdb_hash_table_new (root_table, "tags");
  g_sequence_foreach (self->tags, (GFunc)add_tag_to_table, table);
  g_hash_table_unref (table);

  table = gvdb_hash_table_new (root_table, "bookmarks");
  g_sequence_foreach (self->bookmarks, (GFunc)add_bookmark_to_table, table);
  g_hash_table_unref (table);

  result = gvdb_table_write_contents (root_table, self->gvdb_filename, FALSE, NULL);
  g_hash_table_unref (root_table);

  if (task)
    g_task_return_boolean (task, result);
}

static void
ephy_bookmarks_manager_finalize (GObject *object)
{
  EphyBookmarksManager *self = EPHY_BOOKMARKS_MANAGER (object);

  g_clear_pointer (&self->bookmarks, g_sequence_free);
  g_clear_pointer (&self->tags, g_sequence_free);

  G_OBJECT_CLASS (ephy_bookmarks_manager_parent_class)->finalize (object);
}

static void
ephy_bookmarks_manager_class_init (EphyBookmarksManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ephy_bookmarks_manager_finalize;

  signals[BOOKMARK_ADDED] =
    g_signal_new ("bookmark-added",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  EPHY_TYPE_BOOKMARK);

  signals[BOOKMARK_REMOVED] =
    g_signal_new ("bookmark-removed",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  EPHY_TYPE_BOOKMARK);

  signals[BOOKMARK_TITLE_CHANGED] =
    g_signal_new ("bookmark-title-changed",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  EPHY_TYPE_BOOKMARK);

  signals[BOOKMARK_URL_CHANGED] =
    g_signal_new ("bookmark-url-changed",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  EPHY_TYPE_BOOKMARK);

  signals[TAG_CREATED] =
    g_signal_new ("tag-created",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);

  signals[TAG_DELETED] =
    g_signal_new ("tag-deleted",
                  EPHY_TYPE_BOOKMARKS_MANAGER,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_INT);
}

static void
ephy_bookmarks_manager_init (EphyBookmarksManager *self)
{
  self->gvdb_filename = g_build_filename (ephy_dot_dir (),
                                          EPHY_BOOKMARKS_FILE,
                                          NULL);

  self->bookmarks = g_sequence_new (g_object_unref);
  self->tags = g_sequence_new (g_free);

  g_sequence_insert_sorted (self->tags,
                            g_strdup ("Favorites"),
                            (GCompareDataFunc)ephy_bookmark_tags_compare,
                            NULL);

  /* Create DB file if it doesn't already exists */
  if (!g_file_test (self->gvdb_filename, G_FILE_TEST_EXISTS))
    ephy_bookmarks_manager_save_to_file (self, NULL);

  ephy_bookmarks_manager_load_from_file (self);
}

static void
bookmark_title_changed_cb (EphyBookmark         *bookmark,
                           GParamSpec           *pspec,
                           EphyBookmarksManager *self)
{
  g_signal_emit (self, signals[BOOKMARK_TITLE_CHANGED], 0, bookmark);
}

static void
bookmark_url_changed_cb (EphyBookmark         *bookmark,
                         GParamSpec           *pspec,
                         EphyBookmarksManager *self)
{
  g_signal_emit (self, signals[BOOKMARK_URL_CHANGED], 0, bookmark);
}

void
ephy_bookmarks_manager_add_bookmark (EphyBookmarksManager *self,
                                     EphyBookmark         *bookmark)
{
  GSequenceIter *iter;
  GSequenceIter *prev_iter;

  g_return_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self));
  g_return_if_fail (EPHY_IS_BOOKMARK (bookmark));

  iter = g_sequence_search (self->bookmarks,
                            bookmark,
                            (GCompareDataFunc)ephy_bookmark_bookmarks_sort_func,
                            NULL);

  prev_iter = g_sequence_iter_prev (iter);
  if (g_sequence_iter_is_end (prev_iter)
      || ephy_bookmark_get_time_added (g_sequence_get (prev_iter)) != ephy_bookmark_get_time_added (bookmark)) {
    g_sequence_insert_before (iter, bookmark);
    g_signal_emit (self, signals[BOOKMARK_ADDED], 0, bookmark);

    ephy_bookmarks_manager_save_to_file_async (self, NULL,
                                               (GAsyncReadyCallback)ephy_bookmarks_manager_save_to_file_warn_on_error_cb,
                                               NULL);

    g_signal_connect_object (bookmark, "notify::title",
                             G_CALLBACK (bookmark_title_changed_cb), self, 0);
    g_signal_connect_object (bookmark, "notify::url",
                             G_CALLBACK (bookmark_url_changed_cb), self, 0);
  }
}

void
ephy_bookmarks_manager_add_bookmarks (EphyBookmarksManager *self,
                                      GSequence            *bookmarks)
{
  GSequenceIter *iter;

  g_return_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self));
  g_return_if_fail (bookmarks != NULL);

  for (iter = g_sequence_get_begin_iter (bookmarks);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    if (!g_sequence_lookup (self->bookmarks,
                            bookmark,
                            (GCompareDataFunc)ephy_bookmark_bookmarks_sort_func,
                            NULL))
      g_sequence_prepend (self->bookmarks, g_object_ref (bookmark));
  }

  g_sequence_sort (self->bookmarks,
                   (GCompareDataFunc)ephy_bookmark_bookmarks_sort_func,
                   NULL);

  ephy_bookmarks_manager_save_to_file_async (self, NULL,
                                             (GAsyncReadyCallback)ephy_bookmarks_manager_save_to_file_warn_on_error_cb,
                                             NULL);
}

void
ephy_bookmarks_manager_remove_bookmark (EphyBookmarksManager *self,
                                        EphyBookmark         *bookmark)
{
  GSequenceIter *iter;

  g_return_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self));
  g_return_if_fail (EPHY_IS_BOOKMARK (bookmark));

  for (iter = g_sequence_get_begin_iter (self->bookmarks);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter)) {
    if (g_strcmp0 (ephy_bookmark_get_url (g_sequence_get (iter)),
                   ephy_bookmark_get_url (bookmark)) == 0)
      break;
  }

  g_signal_emit (self, signals[BOOKMARK_REMOVED], 0, bookmark);
  g_sequence_remove (iter);

  ephy_bookmarks_manager_save_to_file_async (self, NULL,
                                             (GAsyncReadyCallback)ephy_bookmarks_manager_save_to_file_warn_on_error_cb,
                                             NULL);

  g_signal_handlers_disconnect_by_func (bookmark, bookmark_title_changed_cb, self);
  g_signal_handlers_disconnect_by_func (bookmark, bookmark_url_changed_cb, self);
}

EphyBookmark *
ephy_bookmarks_manager_get_bookmark_by_url (EphyBookmarksManager *self,
                                            const char           *url)
{
  GSequenceIter *iter;

  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), FALSE);
  g_return_val_if_fail (url != NULL, FALSE);

  for (iter = g_sequence_get_begin_iter (self->bookmarks);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    if (g_strcmp0 (ephy_bookmark_get_url (bookmark), url) == 0)
      return bookmark;
  }

  return NULL;
}

EphyBookmark *
ephy_bookmarks_manager_get_bookmark_by_id (EphyBookmarksManager *self,
                                           const char           *id)
{
  GSequenceIter *iter;

  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  for (iter = g_sequence_get_begin_iter (self->bookmarks);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    if (g_strcmp0 (ephy_bookmark_get_id (bookmark), id) == 0)
      return bookmark;
  }

  return NULL;
}

void
ephy_bookmarks_manager_create_tag (EphyBookmarksManager *self, const char *tag)
{
  GSequenceIter *tag_iter;
  GSequenceIter *prev_tag_iter;

  g_return_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self));
  g_return_if_fail (tag != NULL);

  tag_iter = g_sequence_search (self->tags,
                                (gpointer)tag,
                                (GCompareDataFunc)ephy_bookmark_tags_compare,
                                NULL);

  prev_tag_iter = g_sequence_iter_prev (tag_iter);
  if (g_sequence_iter_is_end (prev_tag_iter)
      || g_strcmp0 (g_sequence_get (prev_tag_iter), tag) != 0) {
    g_sequence_insert_before (tag_iter, g_strdup (tag));
    g_signal_emit (self, signals[TAG_CREATED], 0, tag);
  }
}

void
ephy_bookmarks_manager_delete_tag (EphyBookmarksManager *self, const char *tag)
{
  GSequenceIter *iter = NULL;
  int position;

  g_return_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self));
  g_return_if_fail (tag != NULL);

  iter = g_sequence_lookup (self->tags,
                            (gpointer)tag,
                            (GCompareDataFunc)ephy_bookmark_tags_compare,
                            NULL);
  g_assert (iter != NULL);

  position = g_sequence_iter_get_position (iter);

  g_sequence_remove (iter);

  /* Also remove the tag from each bookmark if they have it */
  g_sequence_foreach (self->bookmarks, (GFunc)ephy_bookmark_remove_tag, (gpointer)tag);

  g_signal_emit (self, signals[TAG_DELETED], 0, position);
}

gboolean
ephy_bookmarks_manager_tag_exists (EphyBookmarksManager *self, const char *tag)
{
  GSequenceIter *iter;

  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), FALSE);
  g_return_val_if_fail (tag != NULL, FALSE);

  iter = g_sequence_lookup (self->tags,
                            (gpointer)tag,
                            (GCompareDataFunc)ephy_bookmark_tags_compare,
                            NULL);

  return iter != NULL;
}

GSequence *
ephy_bookmarks_manager_get_bookmarks (EphyBookmarksManager *self)
{
  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), NULL);

  return self->bookmarks;
}

GSequence *
ephy_bookmarks_manager_get_bookmarks_with_tag (EphyBookmarksManager *self,
                                               const char           *tag)
{
  GSequence *bookmarks;
  GSequenceIter *iter;

  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), NULL);

  bookmarks = g_sequence_new (g_object_unref);

  if (tag == NULL) {
    for (iter = g_sequence_get_begin_iter (self->bookmarks);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter)) {
      EphyBookmark *bookmark = g_sequence_get (iter);

      if (g_sequence_is_empty (ephy_bookmark_get_tags (bookmark))) {
        g_sequence_insert_sorted (bookmarks,
                                  g_object_ref (bookmark),
                                  (GCompareDataFunc)ephy_bookmark_bookmarks_sort_func,
                                  NULL);
      }
    }
  } else {
    for (iter = g_sequence_get_begin_iter (self->bookmarks);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter)) {
      EphyBookmark *bookmark = g_sequence_get (iter);

      if (ephy_bookmark_has_tag (bookmark, tag))
        g_sequence_insert_sorted (bookmarks,
                                  g_object_ref (bookmark),
                                  (GCompareDataFunc)ephy_bookmark_bookmarks_sort_func,
                                  NULL);
    }
  }

  return bookmarks;
}

static int
compare_smart_bookmarks (EphyBookmark *a,
                         EphyBookmark *b)
{
  return g_utf8_collate (ephy_bookmark_get_title (a), ephy_bookmark_get_title (b));
}

GSequence *
ephy_bookmarks_manager_get_smart_bookmarks (EphyBookmarksManager *self)
{
  GSequence *bookmarks;
  GSequenceIter *iter;

  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), NULL);

  bookmarks = g_sequence_new (g_object_unref);

  for (iter = g_sequence_get_begin_iter (self->bookmarks);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    if (ephy_bookmark_is_smart (bookmark)) {
      g_sequence_insert_sorted (bookmarks,
                                g_object_ref (bookmark),
                                (GCompareDataFunc)compare_smart_bookmarks,
                                NULL);
    }
  }

  return bookmarks;
}

GSequence *
ephy_bookmarks_manager_get_tags (EphyBookmarksManager *self)
{
  g_return_val_if_fail (EPHY_IS_BOOKMARKS_MANAGER (self), NULL);

  return self->tags;
}

void
ephy_bookmarks_manager_save_to_file_async (EphyBookmarksManager *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data)
{
  GTask *task;

  task = g_task_new (self, cancellable, callback, user_data);

  ephy_bookmarks_manager_save_to_file (self, task);

  g_object_unref (task);
}

gboolean
ephy_bookmarks_manager_save_to_file_finish (EphyBookmarksManager *self,
                                            GAsyncResult         *result,
                                            GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
ephy_bookmarks_manager_load_from_file (EphyBookmarksManager *self)
{
  GvdbTable *root_table;
  GvdbTable *table;
  GSequence *bookmarks;
  char **list;
  int length;
  int i;

  /* Create a new table to hold data stored in file. */
  root_table = gvdb_table_new (self->gvdb_filename, TRUE, NULL);
  g_assert (root_table);

  /* Add tags to the bookmark manager's sequence. */
  table = gvdb_table_get_table (root_table, "tags");
  g_assert (table);

  /* Iterate over all keys (url's) in the table. */
  list = gvdb_table_get_names (table, &length);
  for (i = 0; i < length; i++)
    ephy_bookmarks_manager_create_tag (self, list[i]);

  gvdb_table_free (table);

  /* Get bookmarks table */
  table = gvdb_table_get_table (root_table, "bookmarks");
  g_assert (table);

  bookmarks = g_sequence_new (g_object_unref);

  /* Iterate over all keys (url's) in the table. */
  list = gvdb_table_get_names (table, &length);
  for (i = 0; i < length; i++) {
    EphyBookmark *bookmark;
    GVariant *value;
    GVariantIter *iter;
    GSequence *tags;
    char *tag;
    const char *title;
    gint64 time_added;
    char *id;
    double modified;
    gboolean uploaded;

    /* Obtain the correspoding GVariant. */
    value = gvdb_table_get_value (table, list[i]);

    g_variant_get (value, "(x&s&sdbas)", &time_added, &title, &id, &modified, &uploaded, &iter);

    /* Add all stored tags in a GSequence. */
    tags = g_sequence_new (g_free);
    while (g_variant_iter_next (iter, "s", &tag)) {
      g_sequence_insert_sorted (tags, tag,
                                (GCompareDataFunc)ephy_bookmark_tags_compare,
                                NULL);
    }
    g_variant_iter_free (iter);

    /* Create the new bookmark. */
    bookmark = ephy_bookmark_new (list[i], title, tags);
    ephy_bookmark_set_time_added (bookmark, time_added);
    ephy_bookmark_set_id (bookmark, id);
    ephy_bookmark_set_modification_time (bookmark, modified);
    ephy_bookmark_set_is_uploaded (bookmark, uploaded);
    g_sequence_prepend (bookmarks, bookmark);
  }
  ephy_bookmarks_manager_add_bookmarks (self, bookmarks);

  gvdb_table_free (table);
  g_sequence_free (bookmarks);
  gvdb_table_free (root_table);
}

void
ephy_bookmarks_manager_save_to_file_warn_on_error_cb (GObject      *object,
                                                      GAsyncResult *result,
                                                      gpointer      user_data)
{
  EphyBookmarksManager *self = EPHY_BOOKMARKS_MANAGER (object);
  gboolean ret;
  GError *error;

  ret = ephy_bookmarks_manager_save_to_file_finish (self, result, &error);
  if (ret == FALSE) {
    g_warning ("%s", error->message);
    g_error_free (error);
  }
}