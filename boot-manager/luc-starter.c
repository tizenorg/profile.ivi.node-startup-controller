/* vi:set et ai sw=2 sts=2 ts=2: */
/* -
 * Copyright (c) 2012 GENIVI.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
#include <gio/gio.h>

#include <dlt/dlt.h>

#include <boot-manager/boot-manager-service.h>
#include <boot-manager/job-manager.h>
#include <boot-manager/luc-starter.h>



DLT_IMPORT_CONTEXT (boot_manager_context);



/* property identifiers */
enum
{
  PROP_0,
  PROP_JOB_MANAGER,
  PROP_BOOT_MANAGER_SERVICE,
};



static void luc_starter_constructed       (GObject      *object);
static void luc_starter_finalize          (GObject      *object);
static void luc_starter_get_property      (GObject      *object,
                                           guint         prop_id,
                                           GValue       *value,
                                           GParamSpec   *pspec);
static void luc_starter_set_property      (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec);
static gint luc_starter_compare_luc_types (gconstpointer a,
                                           gconstpointer b,
                                           gpointer      user_data);
static void luc_starter_start_next_group  (LUCStarter   *starter);
static void luc_starter_start_app         (const gchar  *app,
                                           LUCStarter   *starter);
static void luc_starter_start_app_finish  (JobManager   *manager,
                                           const gchar  *unit,
                                           const gchar  *result,
                                           GError       *error,
                                           gpointer      user_data);
static void luc_starter_cancel_start      (const gchar  *app,
                                           GCancellable *cancellable,
                                           gpointer      user_data);



struct _LUCStarterClass
{
  GObjectClass __parent__;
};

struct _LUCStarter
{
  GObject             __parent__;

  JobManager         *job_manager;
  BootManagerService *boot_manager_service;

  gchar             **prioritised_types;

  GList              *start_order;
  GHashTable         *start_groups;

  GHashTable         *cancellables;
};



G_DEFINE_TYPE (LUCStarter, luc_starter, G_TYPE_OBJECT);



static void
luc_starter_class_init (LUCStarterClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = luc_starter_constructed;
  gobject_class->finalize = luc_starter_finalize;
  gobject_class->get_property = luc_starter_get_property;
  gobject_class->set_property = luc_starter_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_JOB_MANAGER,
                                   g_param_spec_object ("job-manager",
                                                        "Job Manager",
                                                        "The internal handler of Start()"
                                                        " and Stop() jobs",
                                                        TYPE_JOB_MANAGER,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_BOOT_MANAGER_SERVICE,
                                   g_param_spec_object ("boot-manager-service",
                                                        "boot-manager-service",
                                                        "boot-manager-service",
                                                        BOOT_MANAGER_TYPE_SERVICE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}



static void
luc_starter_init (LUCStarter *starter)
{
}



static void
luc_starter_constructed (GObject *object)
{
  LUCStarter *starter = LUC_STARTER (object);

  /* parse the prioritised LUC types defined at build-time */
  starter->prioritised_types = g_strsplit (PRIORITISED_LUC_TYPES, ",", -1);
}



static void
luc_starter_finalize (GObject *object)
{
  LUCStarter *starter = LUC_STARTER (object);

  /* release start order, and groups */
  if (starter->start_order != NULL)
    g_list_free (starter->start_order);
  if (starter->start_groups != NULL)
    g_hash_table_unref (starter->start_groups);

  /* release the cancellables */
  if (starter->cancellables != NULL)
    g_hash_table_unref (starter->cancellables);

  /* free the prioritised types array */
  g_strfreev (starter->prioritised_types);

  /* release the job manager */
  g_object_unref (starter->job_manager);

  /* release the boot manager service */
  g_object_unref (starter->boot_manager_service);

  (*G_OBJECT_CLASS (luc_starter_parent_class)->finalize) (object);
}



static void
luc_starter_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  LUCStarter *starter = LUC_STARTER (object);

  switch (prop_id)
    {
    case PROP_JOB_MANAGER:
      g_value_set_object (value, starter->job_manager);
      break;
    case PROP_BOOT_MANAGER_SERVICE:
      g_value_set_object (value, starter->boot_manager_service);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
luc_starter_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  LUCStarter *starter = LUC_STARTER (object);

  switch (prop_id)
    {
    case PROP_JOB_MANAGER:
      starter->job_manager = g_value_dup_object (value);
      break;
    case PROP_BOOT_MANAGER_SERVICE:
      starter->boot_manager_service = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gint
luc_starter_compare_luc_types (gconstpointer a,
                               gconstpointer b,
                               gpointer      user_data)
{
  LUCStarter *starter = LUC_STARTER (user_data);
  gchar      *type_a;
  gchar      *type_b;
  gint        n;
  gint        pos_a = G_MAXINT;
  gint        pos_b = G_MAXINT;

  /* try to find the first type in the prioritised types list */
  for (n = 0; pos_a < 0 && starter->prioritised_types[n] != NULL; n++)
    if (g_strcmp0 (starter->prioritised_types[n], type_a) == 0)
      pos_a = n;

  /* try to find the second type in the prioritised types list */
  for (n = 0; pos_b < 0 && starter->prioritised_types[n] != NULL; n++)
    if (g_strcmp0 (starter->prioritised_types[n], type_b) == 0)
      pos_b = n;

  /* this statement has the following effect when sorting:
   * - a and b are prioritised     -> prioritization order is preserved
   * - a is prioritised, b isn't   -> negative return value, a comes first
   * - b is prioritised, a isn't   -> positive return value, b comes first
   * - neither a nor b prioritised -> return value is 0, arbitrary order
   */
  return pos_a - pos_b;
}



static void
luc_starter_start_next_group (LUCStarter *starter)
{
  const gchar *group_name;
  GPtrArray   *group;

  g_return_if_fail (IS_LUC_STARTER (starter));
  g_return_if_fail (starter->start_order != NULL);

  group_name = starter->start_order->data;

  g_debug ("start group '%s'", group_name);

  /* look up the group with this name */
  group = g_hash_table_lookup (starter->start_groups, group_name);
  if (group != NULL)
    {
      /* launch all the applications in the group asynchronously */
      g_ptr_array_foreach (group, (GFunc) luc_starter_start_app, starter);
    }
}



static void
luc_starter_start_app (const gchar *app,
                       LUCStarter  *starter)
{
  GCancellable *cancellable;

  g_return_if_fail (app != NULL && *app != '\0');
  g_return_if_fail (IS_LUC_STARTER (starter));

  g_debug ("start app '%s'", app);

  /* create the new cancellable */
  cancellable = g_cancellable_new ();

  /* store an association between each app and its cancellable, so that it is possible to
   * call g_cancellable_cancel() for each respective app. */
  g_hash_table_insert (starter->cancellables, g_strdup (app), cancellable);

  /* start the service, passing the cancellable */
  job_manager_start (starter->job_manager, app, cancellable,
                     luc_starter_start_app_finish,
                     g_object_ref (starter));
}



static void
luc_starter_start_app_finish (JobManager  *manager,
                              const gchar *unit,
                              const gchar *result,
                              GError      *error,
                              gpointer     user_data)
{
  const gchar *group_name;
  LUCStarter  *starter = LUC_STARTER (user_data);
  GPtrArray   *group;
  gboolean     app_found = FALSE;
  gchar       *message;
  guint        n;

  g_return_if_fail (IS_JOB_MANAGER (manager));
  g_return_if_fail (unit != NULL && *unit != '\0');
  g_return_if_fail (IS_LUC_STARTER (user_data));
  g_return_if_fail (starter->start_order != NULL);

  g_debug ("start app '%s' finish", unit);

  /* respond to errors */
  if (error != NULL)
    {
      message = g_strdup_printf ("Failed to start the LUC application \"%s\": %s",
                                 unit, error->message);
      DLT_LOG (boot_manager_context, DLT_LOG_ERROR, DLT_STRING (message));
      g_free (message);
    }

  /* get the current start group */
  group_name = starter->start_order->data;

  /* look up the apps for this group */
  group = g_hash_table_lookup (starter->start_groups, group_name);
  if (group != NULL)
    {
      /* try to find the current app in the group */
      for (n = 0; !app_found && n < group->len; n++)
        {
          if (g_strcmp0 (unit, g_ptr_array_index (group, n)) == 0)
            app_found = TRUE;
        }

      /* remove the app from the group */
      if (app_found)
        g_ptr_array_remove_index (group, n-1);

      /* check if this was the last app in the group to be started */
      if (group->len == 0)
        {
          g_debug ("start group %s finish", group_name);

          /* remove the group from the groups and the order */
          g_hash_table_remove (starter->start_groups, group_name);
          starter->start_order = g_list_delete_link (starter->start_order,
                                                     starter->start_order);

          /* start the next group if there are any left */
          if (starter->start_order != NULL)
            luc_starter_start_next_group (starter);
        }
    }

  /* remove the association between an app and its cancellable, because the app has
   * finished starting, so cannot be cancelled any more */
  g_hash_table_remove (starter->cancellables, unit);

  /* release the LUCStarter because the operation is finished */
  g_object_unref (starter);
}



static void
luc_starter_cancel_start (const gchar  *app,
                          GCancellable *cancellable,
                          gpointer      user_data)
{
  g_cancellable_cancel (cancellable);
}



LUCStarter *
luc_starter_new (JobManager         *job_manager,
                 BootManagerService *boot_manager_service)
{
  g_return_val_if_fail (IS_JOB_MANAGER (job_manager), NULL);
  g_return_val_if_fail (BOOT_MANAGER_IS_SERVICE (boot_manager_service), NULL);

  return g_object_new (TYPE_LUC_STARTER,
                       "job-manager", job_manager,
                       "boot-manager-service", boot_manager_service,
                       NULL);
}



void
luc_starter_start_groups (LUCStarter *starter)
{
  GVariantIter iter;
  GPtrArray   *group;
  GVariant    *context;
  GError      *error = NULL;
  GList       *lp;
  gchar      **apps;
  gchar       *log_text;
  guint        n;
  gint         type;
  gint        *dup_type;

  g_debug ("prioritised types:");
  for (n = 0; starter->prioritised_types[n] != NULL; n++)
    g_debug ("  %s",  starter->prioritised_types[n]);

  /* clear the start order */
  if (starter->start_order != NULL)
    {
      g_list_free (starter->start_order);
      starter->start_order = NULL;
    }

  /* clear the start groups */
  if (starter->start_groups != NULL)
    {
      g_hash_table_remove_all (starter->start_groups);
    }
  else
    {
      starter->start_groups =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               g_free, (GDestroyNotify) g_ptr_array_free);
    }

  /* clear the mapping between apps and their cancellables */
  if (starter->cancellables != NULL)
    {
      g_hash_table_remove_all (starter->cancellables);
    }
  else
    {
      starter->cancellables =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               g_free, (GDestroyNotify) g_object_unref);
    }

  /* get the current last user context */
  context = boot_manager_service_read_luc (starter->boot_manager_service, &error);
  if (error != NULL)
    {
      if (error->code == G_IO_ERROR_NOT_FOUND)
        {
          DLT_LOG (boot_manager_context, DLT_LOG_INFO,
                   DLT_STRING ("Boot manager could not find the last user context"));
        }
      else
        {
          log_text =
            g_strdup_printf ("Error reading last user context: %s", error->message);
          DLT_LOG (boot_manager_context, DLT_LOG_ERROR, DLT_STRING (log_text));
          g_free (log_text);
        }
      g_error_free (error);
      return;
    }

  /* create groups for all types in the LUC */
  g_variant_iter_init (&iter, context);
  while (g_variant_iter_loop (&iter, "{i^as}", &type, &apps))
    {
      dup_type = g_new0 (gint, 1);
      *dup_type = type;
      group = g_ptr_array_new_with_free_func (g_free);

      for (n = 0; apps != NULL && apps[n] != NULL; n++)
        {
          g_debug ("  group %d app %s", type, apps[n]);
          g_ptr_array_add (group, g_strdup (apps[n]));
        }

      g_hash_table_insert (starter->start_groups, dup_type, group);
    }

  /* generate the start order by sorting the LUC types according to
   * the prioritised types */
  starter->start_order = g_hash_table_get_keys (starter->start_groups);
  starter->start_order = g_list_sort_with_data (starter->start_order,
                                                luc_starter_compare_luc_types,
                                                starter);

  g_debug ("start groups (ordered):");
  for (lp = starter->start_order; lp != NULL; lp = lp->next)
    g_debug ("  %s", (gchar *)lp->data);

  if (starter->start_order != NULL)
    luc_starter_start_next_group (starter);

  g_variant_unref (context);
}

void
luc_starter_cancel (LUCStarter *starter)
{
  g_hash_table_foreach (starter->cancellables, (GHFunc) luc_starter_cancel_start, NULL);
}
