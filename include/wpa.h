/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef WPA_H
#define WPA_H

#include <gio/gio.h>

/**
 * Remove all P2P groups by finding p2p-wlan0-* interfaces and removing them.
 * Uses wpa_cli for WPA operations and system calls for interface management.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean
wpa_remove_all_p2p_groups(void);

/**
 * Flush P2P peer table and service discovery information.
 * Runs: wpa_cli -i wlan0 p2p_flush
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean
wpa_flush_p2p(void);

/**
 * Refresh P2P by removing all groups, flushing, and starting discovery.
 * Sequentially calls: wpa_remove_all_p2p_groups -> wpa_flush_p2p -> wpa_p2p_find
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean
wpa_refresh_p2p(void);

#endif /* WPA_H */
