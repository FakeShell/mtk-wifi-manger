/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "wpa.h"
#include "ip.h"
#include <stdio.h>

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
