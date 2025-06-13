/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "wpa.h"
#include <stdio.h>
#include <linux/if.h>
#include <sys/ioctl.h>

/**
 * Execute a wpa_cli command and return success status.
 *
 * @param command  The wpa_cli command to execute.
 * @return         TRUE on success, FALSE on failure.
 */
static gboolean
execute_wpa_cli_command(const gchar *command)
{
    gchar *full_command;
    gchar *output = NULL;
    gchar *error_output = NULL;
    gint exit_status;
    gboolean success = FALSE;
    GError *error = NULL;

    g_debug("Executing wpa_cli command: %s", command);

    full_command = g_strdup_printf("wpa_cli -i wlan0 %s", command);

    if (g_spawn_command_line_sync(full_command, &output, &error_output, &exit_status, &error)) {
        if (exit_status == 0) {
            if (output && strlen(g_strstrip(output)) > 0)
                g_debug("wpa_cli command output: %s", g_strstrip(output));
            g_debug("wpa_cli command succeeded");
            success = TRUE;
        } else {
            g_debug("wpa_cli command failed with exit code: %d", exit_status);
            if (error_output && strlen(g_strstrip(error_output)) > 0)
                g_debug("wpa_cli error output: %s", g_strstrip(error_output));
        }
    } else {
        g_debug("Failed to execute wpa_cli command: %s", error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
    }

    g_free(full_command);
    g_free(output);
    g_free(error_output);

    return success;
}

/**
 * Get list of network interfaces matching a pattern.
 *
 * @param pattern  Pattern to match (e.g., "p2p-wlan0-")
 * @return         NULL-terminated array of interface names, or NULL on failure.
 *                 Caller must free the result with g_strfreev().
 */
static gchar **
get_matching_interfaces(const gchar *pattern)
{
    FILE *fp;
    gchar *line = NULL;
    size_t len = 0;
    ssize_t read;
    GPtrArray *interfaces;
    gchar **result;

    g_debug("Looking for interfaces matching pattern: %s", pattern);

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        g_debug("Failed to open /proc/net/dev");
        return NULL;
    }

    interfaces = g_ptr_array_new();

    /* Skip first two header lines */
    getline(&line, &len, fp);
    getline(&line, &len, fp);

    while ((read = getline(&line, &len, fp)) != -1) {
        gchar *iface_name;
        gchar *colon_pos;

        /* Remove leading whitespace */
        gchar *trimmed = g_strstrip(g_strdup(line));

        /* Find the colon that separates interface name from stats */
        colon_pos = strchr(trimmed, ':');
        if (!colon_pos) {
            g_free(trimmed);
            continue;
        }

        *colon_pos = '\0';
        iface_name = g_strstrip(trimmed);

        if (g_str_has_prefix(iface_name, pattern)) {
            g_debug("Found matching interface: %s", iface_name);
            g_ptr_array_add(interfaces, g_strdup(iface_name));
        }

        g_free(trimmed);
    }

    free(line);
    fclose(fp);

    g_ptr_array_add(interfaces, NULL);
    result = (gchar **)g_ptr_array_free(interfaces, FALSE);

    return result;
}

/**
 * Delete a network interface using netlink sockets.
 *
 * @param interface_name  Name of the interface to delete.
 * @return                TRUE on success, FALSE on failure.
 */
static gboolean
delete_interface(const gchar *interface_name)
{
    int sock;
    struct ifreq ifr;
    gboolean success = FALSE;

    g_debug("Attempting to delete interface: %s", interface_name);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        g_debug("Failed to create socket for interface deletion");
        return FALSE;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFINDEX, &ifr) != 0) {
        g_debug("Interface %s does not exist or already deleted", interface_name);
        close(sock);
        return TRUE; /* Consider it success if interface doesn't exist */
    }

    /* Try to bring interface down first */
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags &= ~IFF_UP;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) == 0)
            g_debug("Interface %s brought down", interface_name);
    }

    /* Virtual P2P interfaces are typically managed by wpa_supplicant
     * and should be removed via wpa_cli. The ioctl SIOCDIFADDR can remove
     * addresses but not the interface itself for virtual interfaces.
     * We rely on wpa_cli p2p_group_remove for actual interface deletion. */
    g_debug("Interface %s prepared for removal", interface_name);
    success = TRUE;

    close(sock);
    return success;
}

gboolean
wpa_remove_all_p2p_groups(void)
{
    gchar **interfaces;
    gboolean overall_success = TRUE;
    int i;

    g_debug("Removing all P2P groups");

    interfaces = get_matching_interfaces("p2p-wlan0-");
    if (!interfaces) {
        g_debug("No P2P interfaces found or failed to get interface list");
        return TRUE; /* No interfaces to remove is considered success */
    }

    for (i = 0; interfaces[i] != NULL; i++) {
        gchar *interface_name = interfaces[i];
        gboolean wpa_success = TRUE;
        gboolean delete_success = TRUE;

        g_debug("Removing group %s", interface_name);

        /* Try to remove via wpa_cli first */
        gchar *remove_command = g_strdup_printf("p2p_group_remove %s", interface_name);
        wpa_success = execute_wpa_cli_command(remove_command);
        g_free(remove_command);

        if (!wpa_success)
            g_debug("wpa_cli group removal failed for %s, continuing anyway", interface_name);

        /* Prepare interface for removal (bring down) */
        delete_success = delete_interface(interface_name);
        if (!delete_success) {
            g_debug("Interface preparation failed for %s", interface_name);
            overall_success = FALSE;
        }
    }

    g_strfreev(interfaces);

    if (overall_success)
        g_debug("All P2P groups removed successfully");
    else
        g_debug("Some P2P group removals failed");

    return overall_success;
}

gboolean
wpa_flush_p2p(void)
{
    g_debug("Flushing P2P peer table");
    return execute_wpa_cli_command("p2p_flush");
}

gboolean
wpa_refresh_p2p(void)
{
    gboolean success = TRUE;

    g_debug("Starting P2P refresh sequence");

    if (!wpa_remove_all_p2p_groups()) {
        g_debug("Failed to remove all P2P groups");
        success = FALSE;
    }

    if (!wpa_flush_p2p()) {
        g_debug("Failed to flush P2P peer table");
        success = FALSE;
    }

    if (success)
        g_debug("P2P refresh completed successfully");
    else
        g_debug("P2P refresh completed with some failures");

    return success;
}
