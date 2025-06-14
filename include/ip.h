/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef IP_H
#define IP_H

#include <gio/gio.h>

/**
 * Get list of network interfaces matching a pattern.
 *
 * @param pattern  Pattern to match (e.g., "p2p-wlan0-")
 * @return         NULL-terminated array of interface names, or NULL on failure.
 *                 Caller must free the result with g_strfreev().
 */
gchar **get_matching_interfaces(const gchar *pattern);

/**
 * Delete a network interface using netlink sockets.
 *
 * @param interface_name  Name of the interface to delete.
 * @return                TRUE on success, FALSE on failure.
 */
gboolean delete_interface(const gchar *interface_name);

/**
 * Enable or disable WoWLAN magic-packet support on phy0.
 *
 * @param enable  TRUE to enable, FALSE to disable.
 * @return        TRUE on success, FALSE on failure.
 */
gboolean enable_wowlan_magic_packet(gboolean enable);

#endif /* IP_H */
