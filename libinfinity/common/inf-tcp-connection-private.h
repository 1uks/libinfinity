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

#ifndef __INF_TCP_CONNECTION_PRIVATE_H__
#define __INF_TCP_CONNECTION_PRIVATE_H__

#include <libinfinity/common/inf-io.h>
#include <libinfinity/common/inf-keepalive.h>

#include <glib-object.h>

G_BEGIN_DECLS

InfTcpConnection*
_inf_tcp_connection_accepted(InfIo* io,
                             InfNativeSocket socket,
                             InfIpAddress* address,
                             guint port,
                             const InfKeepalive* keepalive,
                             GError** error);

G_END_DECLS

#endif /* __INF_TCP_CONNECTION_PRIVATE_H__ */
