/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "dbus.h"
#include "wmt.h"

static GMainLoop *main_loop = NULL;
static DBusService dbus_service = {0};

static void
signal_handler(int signum)
{
    g_debug("Received signal %d, shutting down...", signum);

    /* Signal the WMT monitor thread to stop */
    g_atomic_int_set(&wmt_shutdown_flag, 1);

    /* Signal the monitor thread via GLib */
    wmt_signal_monitor_shutdown();

    if (main_loop)
        g_main_loop_quit(main_loop);
}

static gpointer
wmt_monitor_thread(gpointer data)
{
    g_debug("Starting WMT monitor thread");
    wmt_start_monitor();
    g_debug("WMT monitor thread finished");
    return NULL;
}

int
main(int argc, char *argv[])
{
    GError *error = NULL;
    GThread *monitor_thread = NULL;
    int exit_code = EXIT_SUCCESS;

    g_debug("Starting MediaTek WiFi Manager");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_dbus_service = &dbus_service;
    if (!dbus_service_init(&dbus_service, &error)) {
        g_critical("Failed to initialize D-Bus service: %s", error->message);
        g_error_free(error);
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    /* Create and start WMT monitor thread */
    monitor_thread = g_thread_new("wmt-monitor", wmt_monitor_thread, NULL);
    if (!monitor_thread) {
        g_critical("Failed to create WMT monitor thread");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    main_loop = g_main_loop_new(NULL, FALSE);
    if (!main_loop) {
        g_critical("Failed to create main loop");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    g_debug("Starting main event loop");

    g_main_loop_run(main_loop);

cleanup:
    g_debug("Cleaning up resources");

    /* Wait for monitor thread to finish */
    if (monitor_thread) {
        g_debug("Waiting for WMT monitor thread to finish");
        g_thread_join(monitor_thread);
        g_debug("WMT monitor thread joined");
    }

    dbus_service_cleanup(&dbus_service);
    g_dbus_service = NULL;

    if (main_loop) {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }

    return exit_code;
}
