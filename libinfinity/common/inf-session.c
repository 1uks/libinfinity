/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-marshal.h>

#include <string.h>

/* TODO: Set buffer to non-editable during synchronization */
/* TODO: Cache requests received by other group members
 * during synchronization and process them afterwards */

typedef struct _InfSessionSync InfSessionSync;
struct _InfSessionSync {
  InfConnectionManagerGroup* group;
  InfXmlConnection* conn;

  guint messages_total;
  guint messages_sent;
  InfSessionSyncStatus status;
};

typedef struct _InfSessionPrivate InfSessionPrivate;
struct _InfSessionPrivate {
  InfConnectionManager* manager;
  InfBuffer* buffer;
  InfUserTable* user_table;
  InfSessionStatus status;

  /* Group of subscribed connections */
  InfConnectionManagerGroup* subscription_group;

  union {
    /* INF_SESSION_SYNCHRONIZING */
    struct {
      InfConnectionManagerGroup* group;
      InfXmlConnection* conn;
      guint messages_total;
      guint messages_received;
      gboolean closing;
    } sync;

    /* INF_SESSION_RUNNING */
    struct {
      GSList* syncs;
    } run;
  } shared;
};

typedef struct _InfSessionXmlData InfSessionXmlData;
struct _InfSessionXmlData {
  InfSession* session;
  xmlNodePtr xml;
};

enum {
  PROP_0,

  /* construct only */
  PROP_CONNECTION_MANAGER,
  PROP_BUFFER,
  PROP_USER_TABLE,

  PROP_SUBSCRIPTION_GROUP, /* read/write */

  /* initial sync, construct only */
  /* TODO: Remove this property, since this is already implied by group as
   * soon as we use a separate group for synchronization. This group than has
   * to contain excatly one connection (but it doesn't matter how is publisher)
   * that will be used. We can perhaps even use send_to_group, though that
   * seems a bit unclean. */
  PROP_SYNC_CONNECTION,
  PROP_SYNC_GROUP,

  PROP_STATUS
};

enum {
  CLOSE,
  SYNCHRONIZATION_PROGRESS,
  SYNCHRONIZATION_COMPLETE,
  SYNCHRONIZATION_FAILED,

  LAST_SIGNAL
};

#define INF_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_SESSION, InfSessionPrivate))

static GObjectClass* parent_class;
static guint session_signals[LAST_SIGNAL];
static GQuark inf_session_sync_error_quark;

/*
 * Utility functions.
 */

static const gchar*
inf_session_sync_strerror(InfSessionSyncError errcode)
{
  switch(errcode)
  {
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE:
    return "Got unexpected XML node during synchronization";
  case INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT:
    return "'id' attribute in user message is missing";
  case INF_SESSION_SYNC_ERROR_ID_IN_USE:
    return "User ID is already in use";
  case INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT:
    return "'name' attribute in user message is missing";
  case INF_SESSION_SYNC_ERROR_NAME_IN_USE:
    return "User Name is already in use";
  case INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED:
    return "The connection was closed unexpectedly";
  case INF_SESSION_SYNC_ERROR_SENDER_CANCELLED:
    return "The sender cancelled the synchronization";
  case INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED:
    return "The receiver cancelled teh synchronization";
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC:
    return "Got begin-of-sync message, but synchronization is already "
           "in progress";
  case INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING:
    return "begin-of-sync message does not contain the number of messages "
           "to expect";
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC:
    return "Got end-of-sync message, but synchronization is still in progress";
  case INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC:
    return "Expected begin-of-sync message as first message during "
           "synchronization";
  case INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC:
    return "Expected end-of-sync message as last message during "
           "synchronization";
  case INF_SESSION_SYNC_ERROR_FAILED:
    return "An unknown synchronization error has occured";
  default:
    return "An error with unknown error code occured";
  }
}

static const gchar*
inf_session_get_sync_error_message(GQuark domain,
                                   guint code)
{
  if(domain == inf_session_sync_error_quark)
    return inf_session_sync_strerror(code);

  return "An error with unknown error domain occured";
}

static GSList*
inf_session_find_sync_item_by_connection(InfSession* session,
                                         InfXmlConnection* conn)
{
  InfSessionPrivate* priv;
  GSList* item;

  priv = INF_SESSION_PRIVATE(session);

  g_return_val_if_fail(priv->status == INF_SESSION_RUNNING, NULL);

  for(item = priv->shared.run.syncs; item != NULL; item = g_slist_next(item))
  {
    if( ((InfSessionSync*)item->data)->conn == conn)
      return item;
  }

  return NULL;
}

static InfSessionSync*
inf_session_find_sync_by_connection(InfSession* session,
                                    InfXmlConnection* conn)
{
  GSList* item;
  item = inf_session_find_sync_item_by_connection(session, conn);

  if(item == NULL) return NULL;
  return (InfSessionSync*)item->data;
}

/* Required by inf_session_release_connection() */
static void
inf_session_connection_notify_status_cb(InfXmlConnection* connection,
                                        GParamSpec* pspec,
                                        gpointer user_data);

static void
inf_session_release_connection(InfSession* session,
                               InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;
  GSList* item;
/*  gboolean has_connection;*/

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_assert(priv->shared.sync.conn == connection);
    g_assert(priv->shared.sync.group != NULL);

#if 0
    has_connection = inf_connection_manager_has_connection(
      priv->manager,
      priv->shared.sync.group,
      connection
    );

    /* If the connection was closed, the connection manager removes the
     * connection from itself automatically, so make sure that it has not
     * already done so. TODO: The connection does this in _after, so we
     * should always be able to do this. */
    if(has_connection == TRUE)
    {
      inf_connection_manager_unref_connection(
        priv->manager,
        priv->shared.sync.group,
        connection
      );
    }
#endif

    inf_connection_manager_group_unref(priv->shared.sync.group);

    priv->shared.sync.conn = NULL;
    priv->shared.sync.group = NULL;
    break;
  case INF_SESSION_RUNNING:
    item = inf_session_find_sync_item_by_connection(session, connection);
    g_assert(item != NULL);

    sync = item->data;

#if 0
    has_connection = inf_connection_manager_has_connection(
      priv->manager,
      sync->group,
      connection
    );

    if(has_connection == TRUE)
    {
      inf_connection_manager_unref_connection(
        priv->manager,
        sync->group,
        connection
      );
    }
#endif

    inf_connection_manager_group_unref(sync->group);

    g_slice_free(InfSessionSync, sync);
    priv->shared.run.syncs = g_slist_delete_link(
      priv->shared.run.syncs,
      item
    );

    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(inf_session_connection_notify_status_cb),
    session
  );

  g_object_unref(G_OBJECT(connection));
}

static void
inf_session_send_sync_error(InfSession* session,
                            GError* error)
{
  InfSessionPrivate* priv;
  xmlNodePtr node;
  gchar code_buf[16];

  priv = INF_SESSION_PRIVATE(session);

  g_return_if_fail(priv->status == INF_SESSION_SYNCHRONIZING);
  g_return_if_fail(priv->shared.sync.conn != NULL);

  node = xmlNewNode(NULL, (const xmlChar*)"sync-error");

  xmlNewProp(
     node,
    (const xmlChar*)"domain",
    (const xmlChar*)g_quark_to_string(error->domain)
  );

  sprintf(code_buf, "%u", (unsigned int)error->code);
  xmlNewProp(node, (const xmlChar*)"code", (const xmlChar*)code_buf);

  inf_connection_manager_group_send_to_connection(
    priv->shared.sync.group,
    priv->shared.sync.conn,
    node
  );
}

/*
 * Signal handlers.
 */
static void
inf_session_connection_notify_status_cb(InfXmlConnection* connection,
                                        GParamSpec* pspec,
                                        gpointer user_data)
{
  InfSession* session;
  InfSessionPrivate* priv;
  InfXmlConnectionStatus status;
  GError* error;

  session = INF_SESSION(user_data);
  priv = INF_SESSION_PRIVATE(session);
  error = NULL;

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    g_set_error(
      &error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED)
    );

    switch(priv->status)
    {
    case INF_SESSION_SYNCHRONIZING:
      g_assert(connection == priv->shared.sync.conn);

      /* Release connection prior to session closure, otherwise,
       * inf_session_close would try to tell the synchronizer that the
       * session is closed, but this is rather senseless because the
       * communication channel just has been closed. */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        error
      );

      /* This is already done by the synchronization failed default signal
       * handler: */
/*      inf_session_close(session);*/
      break;
    case INF_SESSION_RUNNING:
      g_assert(
        inf_session_find_sync_by_connection(session, connection) != NULL
      );

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        error
      );

      break;
    case INF_SESSION_CLOSED:
    default:
      g_assert_not_reached();
      break;
    }

    g_error_free(error);
  }
}

/*
 * GObject overrides.
 */

static void
inf_session_init_sync(InfSession* session)
{
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  /* RUNNING is initial state */
  if(priv->status == INF_SESSION_RUNNING)
  {
    priv->status = INF_SESSION_SYNCHRONIZING;
    priv->shared.sync.conn = NULL;
    priv->shared.sync.messages_total = 0;
    priv->shared.sync.messages_received = 0;
    priv->shared.sync.group = NULL;
    priv->shared.sync.closing = FALSE;
  }
}

static void
inf_session_register_sync(InfSession* session)
{
  /* TODO: Use _constructor */
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  /* Register NetObject when all requirements for initial synchronization
   * are met. */
  if(priv->status == INF_SESSION_SYNCHRONIZING &&
     priv->manager != NULL &&
     priv->shared.sync.conn != NULL &&
     priv->shared.sync.group != NULL)
  {
    inf_connection_manager_group_ref(priv->shared.sync.group);

/*    inf_connection_manager_ref_connection(
      priv->manager,
      priv->shared.sync.group,
      priv->shared.sync.conn
    );*/

    g_signal_connect(
      G_OBJECT(priv->shared.sync.conn),
      "notify::status",
      G_CALLBACK(inf_session_connection_notify_status_cb),
      session
    );
  }
}

static void
inf_session_init(GTypeInstance* instance,
                 gpointer g_class)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(instance);
  priv = INF_SESSION_PRIVATE(session);

  priv->manager = NULL;
  priv->buffer = NULL;
  priv->user_table = NULL;
  priv->status = INF_SESSION_RUNNING;

  priv->shared.run.syncs = NULL;
}

static GObject*
inf_session_constructor(GType type,
                        guint n_construct_properties,
                        GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfSessionPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INF_SESSION_PRIVATE(object);

  /* Create empty user table if property was not initialized */
  if(priv->user_table == NULL)
    priv->user_table = inf_user_table_new();

  return object;
}

static void
inf_session_dispose(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  if(priv->status != INF_SESSION_CLOSED)
  {
    /* Close session. This cancells all running synchronizations and tells
     * everyone that the session no longer exists. */
    inf_session_close(session);
  }

  g_object_unref(G_OBJECT(priv->user_table));
  priv->user_table = NULL;

  g_object_unref(G_OBJECT(priv->buffer));
  priv->buffer = NULL;

  g_object_unref(G_OBJECT(priv->manager));
  priv->manager = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_session_finalize(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_session_set_property(GObject* object,
                         guint prop_id,
                         const GValue* value,
                         GParamSpec* pspec)
{
  InfSession* session;
  InfSessionPrivate* priv;
  InfXmlConnection* conn;
  InfConnectionManagerGroup* group;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_CONNECTION_MANAGER:
    g_assert(priv->manager == NULL); /* construct only */
    priv->manager = INF_CONNECTION_MANAGER(g_value_dup_object(value));
    inf_session_register_sync(session);
    break;
  case PROP_BUFFER:
    g_assert(priv->buffer == NULL); /* construct only */
    priv->buffer = INF_BUFFER(g_value_dup_object(value));
    break;
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct only */
    priv->user_table = INF_USER_TABLE(g_value_dup_object(value));
    break;
  case PROP_SUBSCRIPTION_GROUP:
    if(priv->subscription_group != NULL)
      inf_connection_manager_group_unref(priv->subscription_group);

    priv->subscription_group =
      (InfConnectionManagerGroup*)g_value_dup_boxed(value);

    break;
  case PROP_SYNC_CONNECTION:
    conn = INF_XML_CONNECTION(g_value_get_object(value));
    g_assert(priv->shared.sync.conn == NULL); /* construct only */

    if(conn != NULL)
    {
      inf_session_init_sync(session);
      priv->shared.sync.conn = conn;
      g_object_ref(G_OBJECT(priv->shared.sync.conn));
      inf_session_register_sync(session);
    }

    break;
  case PROP_SYNC_GROUP:
    group = (InfConnectionManagerGroup*)g_value_get_boxed(value);
    g_assert(priv->shared.sync.group == NULL);

    if(group != NULL)
    {
      inf_session_init_sync(session);

      priv->shared.sync.group = group;
      /* ref_group is done in register_sync when all components for
        * initial sync are there */
      inf_session_register_sync(session);
    }

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_session_get_property(GObject* object,
                         guint prop_id,
                         GValue* value,
                         GParamSpec* pspec)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_CONNECTION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->manager));
    break;
  case PROP_BUFFER:
    g_value_set_object(value, G_OBJECT(priv->buffer));
    break;
  case PROP_USER_TABLE:
    g_value_set_object(value, G_OBJECT(priv->user_table));
    break;
  case PROP_SUBSCRIPTION_GROUP:
    g_value_set_boxed(value, priv->subscription_group);
    break;
  case PROP_SYNC_CONNECTION:
    g_assert(priv->status == INF_SESSION_SYNCHRONIZING);
    g_value_set_object(value, G_OBJECT(priv->shared.sync.conn));
    break;
  case PROP_SYNC_GROUP:
    g_assert(priv->status == INF_SESSION_SYNCHRONIZING);
    g_value_set_boxed(value, priv->shared.sync.group);
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * VFunc implementations.
 */

static void
inf_session_to_xml_sync_impl_foreach_func(InfUser* user,
                                          gpointer user_data)
{
  InfSessionXmlData* data;
  xmlNodePtr usernode;

  data = (InfSessionXmlData*)user_data;

  usernode = xmlNewNode(NULL, (const xmlChar*)"sync-user");
  inf_session_user_to_xml(data->session, user, usernode);

  xmlAddChild(data->xml, usernode);
}

static void
inf_session_to_xml_sync_impl(InfSession* session,
                             xmlNodePtr parent)
{
  InfSessionPrivate* priv;
  InfSessionXmlData data;

  priv = INF_SESSION_PRIVATE(session);
  data.session = session;
  data.xml = parent;

  inf_user_table_foreach_user(
    priv->user_table,
    inf_session_to_xml_sync_impl_foreach_func,
    &data
  );
}

static gboolean
inf_session_process_xml_sync_impl(InfSession* session,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  GArray* user_props;
  InfUser* user;
  guint i;

  priv = INF_SESSION_PRIVATE(session);
  session_class = INF_SESSION_GET_CLASS(session);

  g_return_val_if_fail(session_class->get_xml_user_props != NULL, FALSE);

  g_return_val_if_fail(priv->status == INF_SESSION_SYNCHRONIZING, FALSE);
  g_return_val_if_fail(connection == priv->shared.sync.conn, FALSE);

  if(strcmp((const gchar*)xml->name, "sync-user") == 0)
  {
    user_props = session_class->get_xml_user_props(
      session,
      connection,
      xml
    );

    user = inf_session_add_user(
      session,
      (GParameter*)user_props->data,
      user_props->len,
      error
    );

    for(i = 0; i < user_props->len; ++ i)
      g_value_unset(&g_array_index(user_props, GParameter, i).value);

    g_array_free(user_props, TRUE);

    if(user == NULL) return FALSE;
    return TRUE;
  }
  else
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE)
    );

    return FALSE;
  }
}

static gboolean
inf_session_process_xml_run_impl(InfSession* session,
                                 InfXmlConnection* connection,
                                 const xmlNodePtr xml,
                                 GError** error)
{
  /* TODO: Proper error quark and code */
  g_set_error(
    error,
    g_quark_from_static_string("INF_SESSION_ERROR"),
    0,
    "Received unhandled XML message '%s'",
    (const gchar*)xml->name
  );

  return FALSE;
}

static GArray*
inf_session_get_xml_user_props_impl(InfSession* session,
                                    InfXmlConnection* conn,
                                    const xmlNodePtr xml)
{
  InfSessionPrivate* priv;
  GArray* array;
  GParameter* parameter;
  xmlChar* name;
  xmlChar* id;
  xmlChar* status;
#if 0
  xmlChar* connection;
  InfXmlConnection* real_conn;
#endif

  priv = INF_SESSION_PRIVATE(session);
  array = g_array_sized_new(FALSE, FALSE, sizeof(GParameter), 16);

  name = xmlGetProp(xml, (const xmlChar*)"name");
  id = xmlGetProp(xml, (const xmlChar*)"id");
  status = xmlGetProp(xml, (const xmlChar*)"status");
#if 0
  connection = xmlGetProp(xml, (const xmlChar*)"connection");
#endif

  if(id != NULL)
  {
    parameter = inf_session_get_user_property(array, "id");
    g_value_init(&parameter->value, G_TYPE_UINT);
    g_value_set_uint(&parameter->value, strtoul((const gchar*)id, NULL, 10));
    xmlFree(id);
  }

  if(name != NULL)
  {
    parameter = inf_session_get_user_property(array, "name");
    g_value_init(&parameter->value, G_TYPE_STRING);
    g_value_set_string(&parameter->value, (const gchar*)name);
    xmlFree(name);
  }

  if(status != NULL)
  {
    parameter = inf_session_get_user_property(array, "status");
    g_value_init(&parameter->value, INF_TYPE_USER_STATUS);

    if(strcmp((const char*)status, "available") == 0)
      g_value_set_enum(&parameter->value, INF_USER_AVAILABLE);
    else
      /* TODO: Error reporting for get_xml_user_props */
      g_value_set_enum(&parameter->value, INF_USER_UNAVAILABLE);
  }

#if 0
  if(connection != NULL)
  {
    real_conn = inf_connection_manager_group_lookup_connection(
      priv->subscription_group,
      connection
    );

    if(real_conn != NULL)
    {
      parameter = inf_session_get_user_property(array, "connection");
      g_value_init(&parameter->value, INF_TYPE_XML_CONNECTION);
      g_value_set_object(&parameter->value, G_OBJECT(real_conn));
    }
    else
    {
      /* TODO: This should be an error. */
    }
  }
#endif

  return array;
}

static void
inf_session_set_xml_user_props_impl(InfSession* session,
                                    const GParameter* params,
                                    guint n_params,
                                    xmlNodePtr xml)
{
  guint i;
  gchar id_buf[16];
  const gchar* name;
  InfUserStatus status;
#if 0
  InfXmlConnection* conn;
  gchar* remote_address;
#endif

  for(i = 0; i < n_params; ++ i)
  {
    if(strcmp(params[i].name, "id") == 0)
    {
      sprintf(id_buf, "%u", g_value_get_uint(&params[i].value));
      xmlNewProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);
    }
    else if(strcmp(params[i].name, "name") == 0)
    {
      name = g_value_get_string(&params[i].value);
      xmlNewProp(xml, (const xmlChar*)"name", (const xmlChar*)name);
    }
    else if(strcmp(params[i].name, "status") == 0)
    {
      status = g_value_get_enum(&params[i].value);
      switch(status)
      {
      case INF_USER_AVAILABLE:
        inf_xml_util_set_attribute(xml, "status", "available");
        break;
      case INF_USER_UNAVAILABLE:
        inf_xml_util_set_attribute(xml, "status", "unavailable");
        break;
      default:
        g_assert_not_reached();
        break;
      }
    }
/*    else if(strcmp(params[i].name, "connection") == 0)
    {
      connection = INF_XML_CONNECTION(g_value_get_object(&params[i].value));

      g_object_get_property(
        G_OBJECT(connection),
        "remote-address",
        &remote_address,
        NULL
      );

      g_free(addr);
    }*/
  }
}

static gboolean
inf_session_validate_user_props_impl(InfSession* session,
                                     const GParameter* params,
                                     guint n_params,
                                     InfUser* exclude,
                                     GError** error)
{
  InfSessionPrivate* priv;
  const GParameter* parameter;
  const gchar* name;
  InfUser* user;
  guint id;
  
  priv = INF_SESSION_PRIVATE(session);

  parameter = inf_session_lookup_user_property(params, n_params, "id");
  if(parameter == NULL)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT)
    );

    return FALSE;
  }

  id = g_value_get_uint(&parameter->value);
  user = inf_user_table_lookup_user_by_id(priv->user_table, id);

  if(user != NULL && user != exclude)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_ID_IN_USE,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_ID_IN_USE)
    );

    return FALSE;
  }

  parameter = inf_session_lookup_user_property(params, n_params, "name");
  if(parameter == NULL)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT)
    );

    return FALSE;
  }

  name = g_value_get_string(&parameter->value);
  user = inf_user_table_lookup_user_by_name(priv->user_table, name);

  if(user != NULL && user != exclude)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_NAME_IN_USE,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_NAME_IN_USE)
    );

    return FALSE;
  }

  return TRUE;
}

/*
 * InfNetObject implementation.
 */
static gboolean
inf_session_handle_received_sync_message(InfSession* session,
                                         InfXmlConnection* connection,
                                         const xmlNodePtr node,
                                         GError** error)
{
  InfSessionClass* session_class;
  InfSessionPrivate* priv;
  xmlChar* num_messages;
  gboolean result;
  xmlNodePtr xml_reply;
  GError* local_error;

  session_class = INF_SESSION_GET_CLASS(session);
  priv = INF_SESSION_PRIVATE(session);

  g_assert(session_class->process_xml_sync != NULL);
  g_assert(priv->status == INF_SESSION_SYNCHRONIZING);

  if(strcmp((const gchar*)node->name, "sync-cancel") == 0)
  {
    local_error = NULL;

    g_set_error(
      &local_error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_SENDER_CANCELLED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_SENDER_CANCELLED)
    );

    g_signal_emit(
      G_OBJECT(session),
      session_signals[SYNCHRONIZATION_FAILED],
      0,
      connection,
      local_error
    );

#if 0
    /* Synchronization was cancelled by remote site, so release connection
     * prior to closure, otherwise we would try to tell the remote site
     * that the session was closed, but there is no point in this because
     * it just was the other way around. */
    /* Note: This is actually done by the default handler of the 
     * synchronization-failed signal. */
    inf_session_close(session);
#endif
    g_error_free(local_error);

    /* Return FALSE, but do not set error because we handled it. Otherwise,
     * inf_session_net_object_received() would try to send a sync-error
     * to the synchronizer which is pointless as mentioned above. */
    return FALSE;
  }
  else if(strcmp((const gchar*)node->name, "sync-begin") == 0)
  {
    if(priv->shared.sync.messages_total > 0)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC
        )
      );

      return FALSE;
    }
    else
    {
      num_messages = xmlGetProp(node, (const xmlChar*)"num-messages");
      if(num_messages == NULL)
      {
        g_set_error(
          error,
          inf_session_sync_error_quark,
          INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING,
          "%s",
          inf_session_sync_strerror(
            INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING
          )
        );

        return FALSE;
      }
      else
      {
        /* 2 + [...] because we also count this initial sync-begin message
         * and the sync-end. This way, we can use a messages_total of 0 to
         * indicate that we did not yet get a sync-begin, even if the
         * whole sync does not contain any messages. */
        priv->shared.sync.messages_total = 2 + strtoul(
          (const gchar*)num_messages,
          NULL,
          0
        );

        priv->shared.sync.messages_received = 1;
        xmlFree(num_messages);

        g_signal_emit(
          G_OBJECT(session),
          session_signals[SYNCHRONIZATION_PROGRESS],
          0,
          connection,
          1.0 / (double)priv->shared.sync.messages_total
        );
 
        return TRUE;
      }
    }
  }
  else if(strcmp((const gchar*)node->name, "sync-end") == 0)
  {
    ++ priv->shared.sync.messages_received;
    if(priv->shared.sync.messages_received !=
       priv->shared.sync.messages_total)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC
        )
      );

      return FALSE;
    }
    else
    {
      /* Server is waiting for ACK so that he knows the synchronization cannot
       * fail anymore. */
      xml_reply = xmlNewNode(NULL, (const xmlChar*)"sync-ack");

      inf_connection_manager_group_send_to_connection(
/*        priv->manager,*/
        priv->shared.sync.group,
        connection,
        xml_reply
      );

      /* Synchronization complete */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_COMPLETE],
        0,
        connection
      );

      return TRUE;
    }
  }
  else
  {
    if(priv->shared.sync.messages_received == 0)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC
        )
      );

      return FALSE;
    }
    else if(priv->shared.sync.messages_received ==
            priv->shared.sync.messages_total - 1)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC,
        "%s",
        inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC)
      );

      return FALSE;
    }
    else
    {
      result = session_class->process_xml_sync(
        session,
        connection,
        node,
        error
      );

      if(result == FALSE) return FALSE;

      ++ priv->shared.sync.messages_received;

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_PROGRESS],
        0,
        connection,
        (double)priv->shared.sync.messages_received /
          (double)priv->shared.sync.messages_total
      );

      return TRUE;
    }
  }
}

static void
inf_session_net_object_sent(InfNetObject* net_object,
                            InfXmlConnection* connection,
                            const xmlNodePtr node)
{
  InfSession* session;
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  session = INF_SESSION(net_object);
  priv = INF_SESSION_PRIVATE(session);

  if(priv->status == INF_SESSION_RUNNING)
  {
    sync = inf_session_find_sync_by_connection(
      INF_SESSION(net_object),
      connection
    );

    /* This can be any message from some session that is not related to
     * the synchronization, so do not assert here. */
    if(sync != NULL)
    {
      g_assert(sync->messages_sent < sync->messages_total);
        ++ sync->messages_sent;

      g_signal_emit(
        G_OBJECT(net_object),
        session_signals[SYNCHRONIZATION_PROGRESS],
        0,
        connection,
        (gdouble)sync->messages_sent / (gdouble)sync->messages_total
      );

      /* We need to wait for the sync-ack before synchronization is
       * completed so that the synchronizee still has a chance to tell
       * us if something goes wrong. */
    }
  }
}

static void
inf_session_net_object_enqueued(InfNetObject* net_object,
                                InfXmlConnection* connection,
                                const xmlNodePtr node)
{
  InfSessionSync* sync;

  if(strcmp((const gchar*)node->name, "sync-end") == 0)
  {
    /* Remember when the last synchronization messages is enqueued because
     * we cannot cancel any synchronizations beyond that point. */
    sync = inf_session_find_sync_by_connection(
      INF_SESSION(net_object),
      connection
    );

    /* This should really be in the list if the node's name is sync-end,
     * otherwise most probably someone else sent a sync-end message via
     * this net_object. */
    g_assert(sync != NULL);
    g_assert(sync->status == INF_SESSION_SYNC_IN_PROGRESS);

    sync->status = INF_SESSION_SYNC_AWAITING_ACK;
  }
}

static gboolean
inf_session_net_object_received(InfNetObject* net_object,
                                InfXmlConnection* connection,
                                const xmlNodePtr node,
                                GError** error)
{
  InfSessionClass* session_class;
  InfSession* session;
  InfSessionPrivate* priv;
  InfSessionSync* sync;
  gboolean result;
  GQuark domain;
  InfSessionSyncError code;
  xmlChar* domain_attr;
  xmlChar* code_attr;
  GError* local_error;

  session = INF_SESSION(net_object);
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);

    local_error = NULL;
    result = inf_session_handle_received_sync_message(
      session,
      connection,
      node,
      &local_error
    );

    if(result == FALSE && local_error != NULL)
    {
      inf_session_send_sync_error(session, local_error);

      /* Note the default handler resets shared->sync.conn and
       * shared->sync.group. */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        local_error
      );

      g_error_free(local_error);
    }

    /* Synchronization is always ptp only, don't forward */
    return FALSE;
  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    if(sync != NULL)
    {
      if(strcmp((const gchar*)node->name, "sync-error") == 0)
      {
        local_error = NULL;

        /* There was an error during synchronization, cancel remaining
         * messages. */
        inf_connection_manager_group_clear_queue(sync->group, connection);

        domain_attr = xmlGetProp(node, (const xmlChar*)"domain");
        code_attr = xmlGetProp(node, (const xmlChar*)"code");

        if(domain_attr != NULL && code_attr != NULL)
        {
          domain = g_quark_from_string((const gchar*)domain_attr);
          code = strtoul((const gchar*)code_attr, NULL, 0);

          g_set_error(
            &local_error,
            domain,
            code,
            "%s",
            inf_session_get_sync_error_message(domain, code)
          );
        }
        else
        {
          g_set_error(
            &local_error,
            inf_session_sync_error_quark,
            INF_SESSION_SYNC_ERROR_FAILED,
            "%s",
            inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_FAILED)
          );
        }

        if(domain_attr != NULL) xmlFree(domain_attr);
        if(code_attr != NULL) xmlFree(code_attr);

        /* Note the default handler actually removes the sync */
        g_signal_emit(
          G_OBJECT(session),
          session_signals[SYNCHRONIZATION_FAILED],
          0,
          connection,
          local_error
        );

        g_error_free(local_error);
      }
      else if(strcmp((const gchar*)node->name, "sync-ack") == 0)
      {
        if(sync->status == INF_SESSION_SYNC_AWAITING_ACK)
        {
          /* Got ack we were waiting for */
          g_signal_emit(
            G_OBJECT(net_object),
            session_signals[SYNCHRONIZATION_COMPLETE],
            0,
            connection
          );
        }
      }

      /* Synchronization is always ptp only, don't forward */
      return FALSE;
    }
    else
    {
      session_class = INF_SESSION_GET_CLASS(session);
      g_assert(session_class->process_xml_run != NULL);

      return session_class->process_xml_run(session, connection, node, error);
    }

    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * Default signal handlers.
 */

static void
inf_session_close_handler(InfSession* session)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;
  xmlNodePtr xml;
  GError* error;

  priv = INF_SESSION_PRIVATE(session);

  error = NULL;

  g_set_error(
    &error,
    inf_session_sync_error_quark,
    INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED,
    "%s",
    inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED)
  );

  g_object_freeze_notify(G_OBJECT(session));

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    if(priv->shared.sync.closing == FALSE)
    {
      /* So that the "synchronization-failed" default signal handler does
       * does not emit the close signal again: */
      /* TODO: Perhaps we should introduce a INF_SESSION_CLOSING status for
       * that. */
      priv->shared.sync.closing = TRUE;

      inf_session_send_sync_error(session, error);

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        priv->shared.sync.conn,
        error
      );
    }

    inf_connection_manager_group_unref(priv->shared.sync.group);

    break;
  case INF_SESSION_RUNNING:
    /* TODO: Set status of all users (except local) to unavailable? */

    while(priv->shared.run.syncs != NULL)
    {
      sync = (InfSessionSync*)priv->shared.run.syncs->data;

      /* If the sync-end message has already been enqueued within the
       * connection manager, we cannot cancel it anymore, so the remote
       * site will receive the full sync nevertheless, so we do not need
       * to cancel anything. */
      if(sync->status == INF_SESSION_SYNC_IN_PROGRESS)
      {
        inf_connection_manager_group_clear_queue(sync->group, sync->conn);

        xml = xmlNewNode(NULL, (const xmlChar*)"sync-cancel");
        inf_connection_manager_group_send_to_connection(
          sync->group,
          sync->conn,
          xml
        );
      }

      /* We had to cancel the synchronization, so the synchronization
       * actually failed. */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        sync->conn,
        error
      );

      /* TODO: Actually remove that sync!? (Or, is this done in the
       * synchronization failed default handler?) */
    }

    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }

  if(priv->subscription_group != NULL)
  {
    inf_connection_manager_group_unref(priv->subscription_group);
    priv->subscription_group = NULL;

    g_object_notify(G_OBJECT(session), "subscription-group");
  }

  g_error_free(error);

  priv->status = INF_SESSION_CLOSED;
  g_object_notify(G_OBJECT(session), "status");
  g_object_thaw_notify(G_OBJECT(session));
}

static void
inf_session_synchronization_complete_handler(InfSession* session,
                                             InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);

    inf_session_release_connection(session, connection);

    priv->status = INF_SESSION_RUNNING;
    priv->shared.run.syncs = NULL;

    g_object_notify(G_OBJECT(session), "status");
    break;
  case INF_SESSION_RUNNING:
    g_assert(
      inf_session_find_sync_by_connection(session, connection) != NULL
    );

    inf_session_release_connection(session, connection);
    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_session_synchronization_failed_handler(InfSession* session,
                                           InfXmlConnection* connection,
                                           const GError* error)
{
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);
    if(priv->shared.sync.closing == FALSE)
    {
      /* So that the "close" default signal handler does not emit the 
       * "synchronization failed" signal again. */
      priv->shared.sync.closing = TRUE;
      inf_session_close(session);
    }

    break;
  case INF_SESSION_RUNNING:
    g_assert(
      inf_session_find_sync_by_connection(session, connection) != NULL
    );

    inf_session_release_connection(session, connection);
    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * Gype registration.
 */

static void
inf_session_class_init(gpointer g_class,
                       gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfSessionPrivate));

  object_class->constructor = inf_session_constructor;
  object_class->dispose = inf_session_dispose;
  object_class->finalize = inf_session_finalize;
  object_class->set_property = inf_session_set_property;
  object_class->get_property = inf_session_get_property;

  session_class->to_xml_sync = inf_session_to_xml_sync_impl;
  session_class->process_xml_sync = inf_session_process_xml_sync_impl;
  session_class->process_xml_run = inf_session_process_xml_run_impl;

  session_class->get_xml_user_props = inf_session_get_xml_user_props_impl;
  session_class->set_xml_user_props = inf_session_set_xml_user_props_impl;
  session_class->validate_user_props = inf_session_validate_user_props_impl;

  session_class->user_new = NULL;

  session_class->close = inf_session_close_handler;
  session_class->synchronization_progress = NULL;
  session_class->synchronization_complete =
    inf_session_synchronization_complete_handler;
  session_class->synchronization_failed =
    inf_session_synchronization_failed_handler;

  inf_session_sync_error_quark = g_quark_from_static_string(
    "INF_SESSION_SYNC_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION_MANAGER,
    g_param_spec_object(
      "connection-manager",
      "Connection manager",
      "The connection manager used for sending requests",
      INF_TYPE_CONNECTION_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER,
    g_param_spec_object(
      "buffer",
      "Buffer",
      "The buffer in which the document content is stored",
      INF_TYPE_BUFFER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "User table containing the users of the session",
      INF_TYPE_USER_TABLE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SUBSCRIPTION_GROUP,
    g_param_spec_boxed(
      "subscription-group",
      "Subscription group",
      "Connection manager group of subscribed connections",
      INF_TYPE_CONNECTION_MANAGER_GROUP,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SYNC_CONNECTION,
    g_param_spec_object(
      "sync-connection",
      "Synchronizing connection",
      "Connection which synchronizes the initial session state",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SYNC_GROUP,
    g_param_spec_boxed(
      "sync-group",
      "Synchronization group",
      "Connection manager group in which to perform synchronization",
      INF_TYPE_CONNECTION_MANAGER_GROUP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "Session Status",
      "Current status of the session",
      INF_TYPE_SESSION_STATUS,
      INF_SESSION_RUNNING,
      G_PARAM_READABLE
    )
  );

  session_signals[CLOSE] = g_signal_new(
    "close",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, close),
    NULL, NULL,
    inf_marshal_VOID__VOID,
    G_TYPE_NONE,
    0
  );

  session_signals[SYNCHRONIZATION_PROGRESS] = g_signal_new(
    "synchronization-progress",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_progress),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_DOUBLE,
    G_TYPE_NONE,
    2,
    INF_TYPE_XML_CONNECTION,
    G_TYPE_DOUBLE
  );

  session_signals[SYNCHRONIZATION_COMPLETE] = g_signal_new(
    "synchronization-complete",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_complete),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  session_signals[SYNCHRONIZATION_FAILED] = g_signal_new(
    "synchronization-failed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_failed),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_POINTER,
    G_TYPE_NONE,
    2,
    INF_TYPE_XML_CONNECTION,
    G_TYPE_POINTER /* actually a GError* */
  );
}

static void
inf_session_net_object_init(gpointer g_iface,
                            gpointer iface_data)
{
  InfNetObjectIface* iface;
  iface = (InfNetObjectIface*)g_iface;

  iface->sent = inf_session_net_object_sent;
  iface->enqueued = inf_session_net_object_enqueued;
  iface->received = inf_session_net_object_received;
}

GType
inf_session_status_get_type(void)
{
  static GType session_status_type = 0;

  if(!session_status_type)
  {
    static const GEnumValue session_status_type_values[] = {
      {
        INF_SESSION_SYNCHRONIZING,
        "INF_SESSION_SYNCHRONIZING",
        "synchronizing"
      }, {
        INF_SESSION_RUNNING,
        "INF_SESSION_RUNNING",
        "running"
      }, {
        INF_SESSION_CLOSED,
        "INF_SESSION_CLOSED",
        "closed"
      }, {
        0,
        NULL,
        NULL
      }
    };

    session_status_type = g_enum_register_static(
      "InfSessionStatus",
      session_status_type_values
    );
  }

  return session_status_type;
}

GType
inf_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfSessionClass),  /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      inf_session_class_init,   /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof(InfSession),       /* instance_size */
      0,                        /* n_preallocs */
      inf_session_init,         /* instance_init */
      NULL                      /* value_table */
    };

    static const GInterfaceInfo net_object_info = {
      inf_session_net_object_init,
      NULL,
      NULL
    };

    session_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfSession",
      &session_type_info,
      0
    );

    g_type_add_interface_static(
      session_type,
      INF_TYPE_NET_OBJECT,
      &net_object_info
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/** inf_session_lookup_user_property:
 *
 * @array: A #GArray containing #GParameter values.
 *
 * Looks up the parameter with the given name in @array.
 *
 * Return Value: A #GParameter, or %NULL.
 **/
const GParameter*
inf_session_lookup_user_property(const GParameter* params,
                                 guint n_params,
                                 const gchar* name)
{
  guint i;

  g_return_val_if_fail(params != NULL || n_params == 0, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for(i = 0; i < n_params; ++ i)
    if(strcmp(params[i].name, name) == 0)
      return &params[i];

  return NULL;
}

/** inf_session_get_user_property:
 *
 * @array: A #GArray containing #GParameter values.
 *
 * Looks up the paremeter with the given name in @array. If there is no such
 * parameter, a new one will be created.
 *
 * Return Value: A #GParameter.
 **/
GParameter*
inf_session_get_user_property(GArray* array,
                              const gchar* name)
{
  GParameter* parameter;
  guint i;

  g_return_val_if_fail(array != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for(i = 0; i < array->len; ++ i)
    if(strcmp(g_array_index(array, GParameter, i).name, name) == 0)
      return &g_array_index(array, GParameter, i);

  g_array_set_size(array, array->len + 1);
  parameter = &g_array_index(array, GParameter, array->len - 1);

  parameter->name = name;
  memset(&parameter->value, 0, sizeof(GValue));
  return parameter;
}

/** inf_session_user_to_xml:
 *
 * @session: A #InfSession.
 * @user: A #InfUser contained in @session.
 * @xml: An XML node to which to add user information.
 *
 * This is a convenience function that queries @user's properties and
 * calls set_xml_user_props with them. This adds the properties of @user
 * to @xml.
 *
 * An equivalent user object may be built by calling the get_xml_user_props
 * vfunc on @xml and then calling the user_new vfunc with the resulting
 * properties.
 **/
void
inf_session_user_to_xml(InfSession* session,
                        InfUser* user,
                        xmlNodePtr xml)
{
  InfSessionClass* session_class;
  GParamSpec** pspecs;
  GParameter* params;
  guint n_params;
  guint i;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));
  g_return_if_fail(xml != NULL);

  session_class = INF_SESSION_GET_CLASS(session);
  g_return_if_fail(session_class->set_xml_user_props != NULL);

  pspecs = g_object_class_list_properties(
    G_OBJECT_CLASS(INF_USER_GET_CLASS(user)),
    &n_params
  );

  params = g_malloc(n_params * sizeof(GParameter));
  for(i = 0; i < n_params; ++ i)
  {
    params[i].name = pspecs[i]->name;
    memset(&params[i].value, 0, sizeof(GValue));
    g_value_init(&params[i].value, pspecs[i]->value_type);
    g_object_get_property(G_OBJECT(user), params[i].name, &params[i].value);
  }

  session_class->set_xml_user_props(session, params, n_params, xml);

  for(i = 0; i < n_params; ++ i)
    g_value_unset(&params[i].value);

  g_free(params);
  g_free(pspecs);
}

/** inf_session_close:
 *
 * @session: A #InfSession.
 *
 * Closes a running session. When a session is closed, it unrefs all
 * connections and no longer handles requests.
 */
void
inf_session_close(InfSession* session)
{
  g_return_if_fail(INF_IS_SESSION(session));
  g_signal_emit(G_OBJECT(session), session_signals[CLOSE], 0);
}

/** inf_session_get_connection_manager:
 *
 * @session: A #InfSession.
 *
 * Returns the connection manager for @session.
 *
 * Return Value: A #InfConnectionManager.
 **/
InfConnectionManager*
inf_session_get_connection_manager(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->manager;
}

/** inf_session_get_buffer:
 *
 * @session: A #InfSession.
 *
 * Returns the buffer used by @session.
 *
 * Return Value: A #InfBuffer.
 **/
InfBuffer*
inf_session_get_buffer(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->buffer;
}

/** inf_session_get_user_table:
 *
 * @session:A #InfSession.
 *
 * Returns the user table used by @session.
 *
 * Return Value: A #InfUserTable.
 **/
InfUserTable*
inf_session_get_user_table(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->user_table;
}

/** inf_session_get_status:
 *
 * @session: A #InfSession.
 *
 * Returns the session's status.
 *
 * Return Value: The status of @session.
 **/
InfSessionStatus
inf_session_get_status(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), INF_SESSION_CLOSED);
  return INF_SESSION_PRIVATE(session)->status;
}

/** inf_session_add_user:
 *
 * @session A #InfSession.
 * @params: Construction parameters for the #InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Adds a user to @session. The user object is constructed via the
 * user_new vfunc of #InfSessionClass. This will create a new #InfUser
 * object by default, but may be overridden by subclasses to create
 * different kinds of users.
 *
 * Return Value: The new #InfUser, or %NULL in case of an error.
 **/
InfUser*
inf_session_add_user(InfSession* session,
                     const GParameter* params,
                     guint n_params,
                     GError** error)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  InfUser* user;
  gboolean result;

  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  session_class = INF_SESSION_GET_CLASS(session);

  g_return_val_if_fail(session_class->validate_user_props != NULL, NULL);
  g_return_val_if_fail(session_class->user_new != NULL, NULL);

  priv = INF_SESSION_PRIVATE(session);

  result = session_class->validate_user_props(
    session,
    params,
    n_params,
    NULL,
    error
  );

  if(result == TRUE)
  {
    user = session_class->user_new(session, params, n_params);
    inf_user_table_add_user(priv->user_table, user);

    return user;
  }

  return NULL;
}

/** inf_session_synchronize_to:
 *
 * @session: A #InfSession with state %INF_SESSION_RUNNING.
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfConnection.
 *
 * Initiates a synchronization to @connection. On the other end of
 * @connection, a new session with the sync-connection and sync-group
 * construction properties set should have been created. @group is used
 * as a group in the connection manager. It is allowed for @group to have
 * another #InfNetObject than @session, however, you should forward the
 * #InfNetObject messages your object receives to @session then. Also,
 * @connection must already be present in @group, and should not be removed
 * until synchronization finished.
 *
 * A synchronization can only be initiated if @session is in state
 * %INF_SESSION_RUNNING.
 **/
void
inf_session_synchronize_to(InfSession* session,
                           InfConnectionManagerGroup* group,
                           InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  InfSessionSync* sync;
  xmlNodePtr messages;
  xmlNodePtr next;
  xmlNodePtr xml;
  gchar num_messages_buf[16];

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  priv = INF_SESSION_PRIVATE(session);

  g_return_if_fail(priv->status == INF_SESSION_RUNNING);
  g_return_if_fail(
    inf_session_find_sync_by_connection(session, connection) == NULL
  );

  session_class = INF_SESSION_GET_CLASS(session);
  g_return_if_fail(session_class->to_xml_sync != NULL);

  sync = g_slice_new(InfSessionSync);
  sync->conn = connection;
  sync->messages_sent = 0;
  sync->messages_total = 2; /* including sync-begin and sync-end */
  sync->status = INF_SESSION_SYNC_IN_PROGRESS;

  g_object_ref(G_OBJECT(connection));
  priv->shared.run.syncs = g_slist_prepend(priv->shared.run.syncs, sync);

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(inf_session_connection_notify_status_cb),
    session
  );

  sync->group = group;
  inf_connection_manager_group_ref(sync->group);

  /* The group needs to contain that connection, of course. */
  g_assert(
    inf_connection_manager_group_has_connection(sync->group, connection)
  );

  /* Name is irrelevant because the node is only used to collect the child
   * nodes via the to_xml_sync vfunc. */
  messages = xmlNewNode(NULL, (const xmlChar*)"sync-container");
  session_class->to_xml_sync(session, messages);

  for(xml = messages->children; xml != NULL; xml = xml->next)
    ++ sync->messages_total;

  sprintf(num_messages_buf, "%u", sync->messages_total - 2);

  xml = xmlNewNode(NULL, (const xmlChar*)"sync-begin");

  xmlNewProp(
    xml,
    (const xmlChar*)"num-messages",
    (const xmlChar*)num_messages_buf
  );

  inf_connection_manager_group_send_to_connection(
    sync->group,
    connection,
    xml
  );

  /* TODO: Add a function that can send multiple messages */
  for(xml = messages->children; xml != NULL; xml = next)
  {
    next = xml->next;
    xmlUnlinkNode(xml);

    inf_connection_manager_group_send_to_connection(
      sync->group,
      connection,
      xml
    );
  }

  xmlFreeNode(messages);

  xml = xmlNewNode(NULL, (const xmlChar*)"sync-end");

  inf_connection_manager_group_send_to_connection(
    sync->group,
    connection,
    xml
  );
}

/** inf_session_get_synchronization_status:
 *
 * @session: A #InfSession.
 * @connection: A #InfXmlConnection.
 *
 * If @session is in status %INF_SESSION_SYNCHRONIZING, this always returns
 * %INF_SESSION_SYNC_IN_PROGRESS if @connection is the connection with which
 * the session is synchronized, and %INF_SESSION_SYNC_NONE otherwise.
 *
 * If @session is in status %INF_SESSION_RUNNING, this returns the status
 * of the synchronization to @connection. %INF_SESSION_SYNC_NONE is returned,
 * when there is currently no synchronization ongoing to @connection,
 * %INF_SESSION_SYNC_IN_PROGRESS is returned, if there is one, and
 * %INF_SESSION_SYNC_AWAITING_ACK if the synchronization is finished but we
 * are waiting for the acknowledgement from the remote site that all
 * synchronization data has been progressed successfully. The synchronization
 * can still fail in this state but it can no longer by cancelled.
 *
 * If @session is in status $INF_SESSION_CLOSED, this always returns
 * %INF_SESSION_SYNC_NONE.
 *
 * Return Value: The synchronization status of @connection.
 **/
InfSessionSyncStatus
inf_session_get_synchronization_status(InfSession* session,
                                       InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  g_return_val_if_fail(INF_IS_SESSION(session), INF_SESSION_SYNC_NONE);

  g_return_val_if_fail(
    INF_IS_XML_CONNECTION(connection),
    INF_SESSION_SYNC_NONE
  );

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    if(connection == priv->shared.sync.conn)
      return INF_SESSION_SYNC_IN_PROGRESS;
    return INF_SESSION_SYNC_NONE;
  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    if(sync == NULL) return INF_SESSION_SYNC_NONE;

    return sync->status;
  case INF_SESSION_CLOSED:
    return INF_SESSION_SYNC_NONE;
  default:
    g_assert_not_reached();
    break;
  }
}

/** inf_session_get_synchronization_progress:
 *
 * @session: A #InfSession.
 * @connection: A #InfXmlConnection.
 *
 * This function requires that the synchronization status of @connection
 * is %INF_SESSION_SYNC_IN_PROGRESS or %INF_SESSION_SYNC_AWAITING_ACK
 * (see inf_session_get_synchronization_status()). Then, it returns a value
 * between 0.0 and 1.0 specifying how much synchronization data has already
 * been transferred to the remote site.
 *
 * Note that if the session is in status %INF_SESSION_RUNNING, it is
 * possible that this function returns 1.0 (i.e. all data has been
 * transmitted) but the synchronization is not yet complete, because the
 * remote site must still acknowledge the synchronization. The synchronization
 * then is in status %INF_SESSION_SYNC_AWAITING_ACK.
 *
 * Return Value: A value between 0.0 and 1.0.
 **/
gdouble
inf_session_get_synchronization_progress(InfSession* session,
                                         InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  g_return_val_if_fail(INF_IS_SESSION(session), 0.0);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), 0.0);

  g_return_val_if_fail(
    inf_session_get_synchronization_status(
      session,
      connection
    ) != INF_SESSION_SYNC_NONE,
    0.0
  );

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);
    return (gdouble)priv->shared.sync.messages_received /
           (gdouble)priv->shared.sync.messages_total;

  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    g_assert(sync != NULL);

    return (gdouble)sync->messages_sent / (gdouble)sync->messages_total;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    return 0.0;
  }
}

/** inf_session_get_subscription_group:
 *
 * @session: A #InfSession.
 *
 * Returns the subscription group for @session, if any.
 *
 * Return Value: A #InfConnectionManagerGroup, or %NULL.
 **/
InfConnectionManagerGroup*
inf_session_get_subscription_group(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->subscription_group;
}

/** inf_session_set_subscription_group:
 *
 * @session: A #InfSession.
 * @group: A #InfConnectionManagerGroup.
 *
 * Sets the subscription group for @session. The subscription group is the
 * group in which all connections subscribed to the session are a member of.
 *
 * #InfSession itself does not deal with subscriptions, so it is your job
 * to keep @group up-to-date (for example if you add non-local users to
 * @session). This is normally done by a so-called session proxy such as
 * #InfcSessionProxy or #InfdSessionProxy, respectively.
 **/
void
inf_session_set_subscription_group(InfSession* session,
                                   InfConnectionManagerGroup* group)
{
  InfSessionPrivate* priv;

  g_return_if_fail(INF_IS_SESSION(session));

  priv = INF_SESSION_PRIVATE(session);

  if(priv->subscription_group != group)
  {
    if(priv->subscription_group != NULL)
      inf_connection_manager_group_unref(priv->subscription_group);

    priv->subscription_group = group;

    if(group != NULL)
      inf_connection_manager_group_ref(group);

    g_object_notify(G_OBJECT(session), "subscription-group");
  }
}

/** inf_session_send_to_subscriptions:
 *
 * @session: A #InfSession.
 * @except: A #InfXmlConnection, or %NULL.
 * @xml: The message to send.
 *
 * Sends a XML message to the all members of @session's subscription group,
 * except @except. This function can only be called if the subscription group
 * is non-%NULL. It takes ownership of @xml.
 **/
void
inf_session_send_to_subscriptions(InfSession* session,
                                  InfXmlConnection* except,
                                  xmlNodePtr xml)
{
  InfSessionPrivate* priv;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(except == NULL || INF_IS_XML_CONNECTION(except));
  g_return_if_fail(xml != NULL);

  priv = INF_SESSION_PRIVATE(session);
  g_return_if_fail(priv->subscription_group != NULL);

  inf_connection_manager_group_send_to_group(
    priv->subscription_group,
    except,
    xml
  );
}

/* vim:set et sw=2 ts=2: */
