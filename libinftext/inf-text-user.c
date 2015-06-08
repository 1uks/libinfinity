/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <libinftext/inf-text-user.h>

typedef struct _InfTextUserPrivate InfTextUserPrivate;
struct _InfTextUserPrivate {
  guint caret;
  gint selection;
  gdouble hue;
};

enum {
  PROP_0,

  PROP_CARET,
  PROP_SELECTION,
  PROP_HUE
};

enum {
  SELECTION_CHANGED,

  LAST_SIGNAL
};

#define INF_TEXT_USER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_USER, InfTextUserPrivate))

static guint user_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE(InfTextUser, inf_text_user, INF_ADOPTED_TYPE_USER,
  G_ADD_PRIVATE(InfTextUser))

static void
inf_text_user_init(InfTextUser* user)
{
  InfTextUserPrivate* priv;
  priv = INF_TEXT_USER_PRIVATE(user);

  priv->caret = 0;
  priv->selection = 0;
  priv->hue = 0.0;
}

static void
inf_text_user_set_property(GObject* object,
                           guint prop_id,
                           const GValue* value,
                           GParamSpec* pspec)
{
  InfTextUser* user;
  InfTextUserPrivate* priv;

  user = INF_TEXT_USER(object);
  priv = INF_TEXT_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_CARET:
    priv->caret = g_value_get_uint(value);
    break;
  case PROP_SELECTION:
    priv->selection = g_value_get_int(value);
    break;
  case PROP_HUE:
    priv->hue = g_value_get_double(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_user_get_property(GObject* object,
                           guint prop_id,
                           GValue* value,
                           GParamSpec* pspec)
{
  InfTextUser* user;
  InfTextUserPrivate* priv;

  user = INF_TEXT_USER(object);
  priv = INF_TEXT_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_CARET:
    g_value_set_uint(value, priv->caret);
    break;
  case PROP_SELECTION:
    g_value_set_int(value, priv->selection);
    break;
  case PROP_HUE:
    g_value_set_double(value, priv->hue);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_user_selection_changed(InfTextUser* user,
                                guint position,
                                guint length,
                                gboolean by_request)
{
  InfTextUserPrivate* priv;
  priv = INF_TEXT_USER_PRIVATE(user);

  priv->caret = position;
  priv->selection = length;

  g_object_notify(G_OBJECT(user), "caret-position");
  g_object_notify(G_OBJECT(user), "selection-length");
}

static void
inf_text_user_class_init(InfTextUserClass* user_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(user_class);

  object_class->set_property = inf_text_user_set_property;
  object_class->get_property = inf_text_user_get_property;

  user_class->selection_changed = inf_text_user_selection_changed;

  g_object_class_install_property(
    object_class,
    PROP_CARET,
    g_param_spec_uint(
      "caret-position",
      "Caret position",
      "The position of this user's caret",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SELECTION,
    g_param_spec_int(
      "selection-length",
      "Selection length",
      "The number of characters of this user's selection",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_HUE,
    g_param_spec_double(
      "hue",
      "Hue",
      "The hue value of the user's color. saturation and lightness are set "
      "by each client individually.",
      0.0,
      1.0,
      0.0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  user_signals[SELECTION_CHANGED] = g_signal_new(
    "selection-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTextUserClass, selection_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    G_TYPE_UINT,
    G_TYPE_INT,
    G_TYPE_BOOLEAN
  );
}

/**
 * inf_text_user_new: (constructor)
 * @id: The user ID for this user.
 * @name: The user's name.
 * @vector: (allow-none): The state at which the user is at, or %NULL.
 * @hue: The hue value of the user's color.
 *
 * Creates a new #InfTextUser. @id should be unique for all users working
 * together. #InfUserTable will refuse to add users with duplicate id. If
 * @vector is %NULL, then the vector with all components zero is used.
 *
 * Returns: (transfer full): A new #InfTextUser. Free with g_object_unref()
 * when no longer needed.
 */
InfTextUser*
inf_text_user_new(guint id,
                  const gchar* name,
                  InfAdoptedStateVector* vector,
                  double hue)
{
  g_return_val_if_fail(name != NULL, NULL);

  return INF_TEXT_USER(
    g_object_new(
      INF_TEXT_TYPE_USER,
      "id", id,
      "name", name,
      "vector", vector,
      "hue", hue,
      NULL
    )
  );
}

/**
 * inf_text_user_get_caret_position:
 * @user: A #InfTextUser.
 *
 * Returns the position of @user's caret.
 *
 * Returns: @user's caret position.
 **/
guint
inf_text_user_get_caret_position(InfTextUser* user)
{
  g_return_val_if_fail(INF_TEXT_IS_USER(user), 0);
  return INF_TEXT_USER_PRIVATE(user)->caret;
}

/**
 * inf_text_user_get_selection_length:
 * @user: A #InfTextUser.
 *
 * Returns the number of characters this user has selected, starting from
 * the caret position. Negative number mean selection towards the beginning
 * of the buffer.
 *
 * Returns: @user's selection length in characters.
 **/
gint
inf_text_user_get_selection_length(InfTextUser* user)
{
  g_return_val_if_fail(INF_TEXT_IS_USER(user), 0);
  return INF_TEXT_USER_PRIVATE(user)->selection;
}

/**
 * inf_text_user_set_selection:
 * @user: A #InfTextUser.
 * @position: The new position for the user's caret.
 * @length: The number of characters to select. Negative numbers mean
 * selection towards the beginning.
 * @by_request: %TRUE if explicitly requested or %FALSE when just an effect
 * of another operation.
 *
 * Changes @user's selection (i.e. caret position and selection length). The
 * @by_request parameter should be set to %TRUE if the selection change was
 * requested explicitly, for example by the user actively moving the cursor
 * using the mouse or the keyboard. If the cursor position changes only
 * because another user inserted text at a position before the user's cursor
 * and this results in a change of the selection position, the @by_request
 * parameter should be set to %FALSE.
 **/
void
inf_text_user_set_selection(InfTextUser* user,
                            guint position,
                            gint length,
                            gboolean by_request)
{
  g_return_if_fail(INF_TEXT_IS_USER(user));
  g_signal_emit(
    G_OBJECT(user),
    user_signals[SELECTION_CHANGED],
    0,
    position,
    length,
    by_request
  );
}

/**
 * inf_text_user_get_hue:
 * @user: A #InfTextUser.
 *
 * Returns the hue of the user's color as a double ranging from 0 to 1.
 * The other components (saturation and lightness) are not specific to the
 * user and may be chosen indivudually to optimize the actual visual display.
 *
 * Returns: The hue of the @user's color.
 **/
gdouble
inf_text_user_get_hue(InfTextUser* user)
{
  g_return_val_if_fail(INF_TEXT_IS_USER(user), 0.0);
  return INF_TEXT_USER_PRIVATE(user)->hue;
}

/* vim:set et sw=2 ts=2: */
