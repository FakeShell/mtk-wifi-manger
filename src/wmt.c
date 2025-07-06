/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2018 MediaTek
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "wmt.h"
#include "dbus.h"
#include "ip.h"
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <hybris/properties/properties.h>

/* Global shutdown flag */
volatile gint wmt_shutdown_flag = 0;

/* Global context for the monitor thread */
static GMainContext *monitor_context = NULL;
static GMainLoop *monitor_loop = NULL;

static int
restart_systemd_service(const char *service_name)
{
    GDBusConnection *connection = NULL;
    GVariant *result = NULL;
    GError *error = NULL;
    int ret = -1;

    g_debug("Attempting to restart systemd service: %s", service_name);

    /* Connect to system bus */
    connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        g_debug("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        return ret;
    }

    result = g_dbus_connection_call_sync(connection,
                                         "org.freedesktop.systemd1",
                                         "/org/freedesktop/systemd1",
                                         "org.freedesktop.systemd1.Manager",
                                         "RestartUnit",
                                         g_variant_new("(ss)", service_name, "replace"),
                                         G_VARIANT_TYPE("(o)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         10000, /* 10 second timeout */
                                         NULL,
                                         &error);

    if (!result) {
        g_debug("Failed to restart service %s: %s", service_name, error->message);
        g_error_free(error);
    } else {
        gchar *job_path;
        g_variant_get(result, "(o)", &job_path);
        g_debug("Successfully initiated restart of %s (job: %s)", service_name, job_path);
        g_free(job_path);
        g_variant_unref(result);
        ret = 0;
    }

    g_object_unref(connection);
    return ret;
}

int
wmt_set_state(WiFiState state)
{
    int ret = -1;
    char command_data;

    g_debug("Setting WiFi state to %d", state);

    switch (state) {
        case WIFI_STATE_AP:
            command_data = 'A';
            g_debug("Setting AP mode (writing 'A')");
            enable_wowlan_magic_packet(TRUE);
            break;
        case WIFI_STATE_P2P:
            command_data = 'P';
            g_debug("Setting P2P mode (writing 'P')");
            enable_wowlan_magic_packet(FALSE);
            break;
        case WIFI_STATE_DUAL_AP:
            command_data = 'E';
            g_debug("Setting Dual AP mode (writing 'E')");
            enable_wowlan_magic_packet(TRUE);
            break;
        case WIFI_STATE_DUAL_P2P:
            command_data = 'D';
            g_debug("Setting Dual P2P mode (writing 'D')");
            enable_wowlan_magic_packet(FALSE);
            break;
        case WIFI_STATE_ON:
            command_data = '1';
            g_debug("Setting WiFi ON (writing '1')");
            enable_wowlan_magic_packet(TRUE);
            break;
        case WIFI_STATE_OFF:
            command_data = '0';
            g_debug("Setting WiFi OFF (writing '0')");
            enable_wowlan_magic_packet(FALSE);
            break;
        default:
            g_debug("Invalid WiFi state: %d", state);
            return -1;
    }

    ret = write_data_to_driver(&command_data, 1);
    if (ret < 0) {
        g_debug("Failed to set WiFi state %d", state);
        return -1;
    }

    /* Emit signal about state change */
    dbus_emit_state_changed(g_dbus_service, state);

    g_debug("Successfully set WiFi state to %d", state);

    /* Only restart NetworkManager for mode changes, not ON/OFF states */
    if (state != WIFI_STATE_OFF && state != WIFI_STATE_ON) {
        if (restart_systemd_service("NetworkManager.service") != 0)
            g_debug("Failed to restart NetworkManager service");
        else
            g_debug("NetworkManager restart initiated successfully");
    }

    return 0;
}

int
write_data_to_driver(char *data, size_t length)
{
    int ret = -1;
    int fd = -1;

    g_debug("Writing %zu bytes to driver", length);

    if (!data || !length) {
        g_debug("Invalid input - data=%p, length=%zu", (void*)data, length);
        return ret;
    }

    g_debug("Opening device: %s", WIFI_LOADER_DEV);
    fd = open(WIFI_LOADER_DEV, O_RDWR);
    if (fd == -1) {
        g_debug("Can't open device node(%s), error: %s", WIFI_LOADER_DEV, strerror(errno));
        return ret;
    }

    /* write data to kernel */
    ret = write(fd, data, length);

    if (ret < 0)
        g_debug("Write failed, error: %s", strerror(errno));
    else
        g_debug("Successfully wrote %d bytes", ret);

    close(fd);
    return ret;
}

int
get_custom_mac_address(char mac[])
{
    int fd = -1;
    char buf[BUF_SIZE] = {0};
    int read_len = 0, i = 0;
    int ret_val = 1;

    memset(buf, 0, BUF_SIZE);
    fd = open(WIFI_MACADDR_FILE, O_RDONLY);
    if (fd == -1) {
        g_debug("Unable to access mac file");
        return 0;
    }

    read_len = read(fd, buf, BUF_SIZE - 1);
    if (read_len >= 17) {
        g_debug("MAC ADDR = %s", buf);
        for (i = 0; i < 17; i++)
            if (buf[i] == ':')
                buf[i] = 0;
        for (i = 0; i < 6; i++) {
            mac[i] = strtol(&buf[i * 3], NULL, 16);
            g_debug("mac[%d] = %x", i, mac[i]);
        }
    } else {
        ret_val = 0;
    }

    close(fd);
    return ret_val;
}

int
write_nvram(char *filename)
{
    int ret = -1;
    int i = 0, fd = -1;
    char *acnvram;
    struct stat stat_nvram;
    int nvram_size = 0;
    char mac[6] = {0};
    int read_len = 0;

    g_debug("Starting NVRAM write process for %s", filename);

    /* sleep 1 more second in case that daemon is still writing */
    for (i = 0; i < MAX_RETRY_COUNT; i++) {
        g_debug("Attempt %d/%d: Checking file status", i + 1, MAX_RETRY_COUNT);

        if (stat(filename, &stat_nvram) == -1) {
            g_debug("Cannot stat %s - %s", filename, strerror(errno));
            sleep(1);
            continue;
        }

        nvram_size = stat_nvram.st_size - 2;
        g_debug("File size: %ld, NVRAM size: %d", stat_nvram.st_size, nvram_size);

        if (nvram_size > 0 && (nvram_size & 0x0ff) == 0) {
            g_debug("Valid NVRAM size found");
            break;
        }

        g_debug("Invalid size, retrying...");
        sleep(1);
    }

    if (nvram_size <= 0 || (nvram_size & 0x0ff) != 0) {
        g_debug("Invalid NVRAM size %d", nvram_size);
        return ret;
    }

    acnvram = (char *)malloc(nvram_size + 12);
    if (!acnvram) {
        g_debug("Failed to allocate memory");
        return ret;
    }

    memset(acnvram, 0, nvram_size + 12);
    strncpy(acnvram, "WR-BUF:NVRAM", 12);

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        g_debug("Cannot open file - %s", strerror(errno));
        free(acnvram);
        return ret;
    }

    read_len = read(fd, acnvram + 12, nvram_size);
    g_debug("Read %d bytes from NVRAM file", read_len);
    close(fd);

    if (read_len <= 0) {
        g_debug("Failed to read NVRAM data");
        free(acnvram);
        return ret;
    }

    if (get_custom_mac_address(mac)) {
        g_debug("Successfully got MAC address, copying to NVRAM buffer");
        memcpy(acnvram + 12 + NVRAM_MAC_ADDRESS_OFFSET, mac, sizeof(mac));
    } else {
        g_debug("Failed to get custom MAC address");
    }

    g_debug("Writing NVRAM data to driver (%d bytes)", 12 + nvram_size);
    ret = write_data_to_driver(acnvram, 12 + nvram_size);

    if (ret < 0) {
        g_debug("Failed to write NVRAM to driver");
    } else {
        g_debug("Successfully wrote NVRAM to driver");
        g_debug("Setting vendor.mtk.nvram.ready property");
        property_set("vendor.mtk.nvram.ready", "1");

        /* Turn WiFi ON after NVRAM is ready */
        g_debug("Turning WiFi ON after NVRAM initialization");
        wmt_set_state(WIFI_STATE_ON);
    }

    free(acnvram);
    return ret;
}

void
get_custom_nvram_file_name(char *filename)
{
    int fd = -1;
    char buf[BUF_SIZE] = {0};
    int read_len = 0;
    size_t remaining_space;

    memset(buf, 0, BUF_SIZE);
    fd = open(WIFI_NVRAM_INI_FILE, O_RDONLY);
    if (fd == -1) {
        remaining_space = BUF_SIZE - strlen(filename);
        if (remaining_space > 4)
            strcat(filename, "WIFI");
    } else {
        read_len = read(fd, buf, BUF_SIZE - 1);
        if (read_len > 0 && read_len < BUF_SIZE - 1) {
            buf[read_len] = 0;
            remaining_space = BUF_SIZE - strlen(filename);
            if (remaining_space > read_len)
                strcat(filename, buf);
        } else {
            remaining_space = BUF_SIZE - strlen(filename);
            if (remaining_space > 4)
                strcat(filename, "WIFI");
        }

        close(fd);
    }

    g_debug("Custom NVRAM filename = %s", filename);
}

static gboolean
on_inotify_event(GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
    char *nvram_filename = (char *)user_data;
    char buf[BUF_SIZE];
    gssize bytes_read;
    gsize bytes_to_read;
    GError *error = NULL;
    struct inotify_event *event;
    gsize offset = 0;

    if (condition & (G_IO_HUP | G_IO_ERR)) {
        g_debug("Inotify channel error or hangup");
        if (monitor_loop)
            g_main_loop_quit(monitor_loop);
        return FALSE;
    }

    if (!(condition & G_IO_IN))
        return TRUE;

    /* Read inotify events */
    bytes_to_read = sizeof(buf);
    if (g_io_channel_read_chars(channel, buf, bytes_to_read, &bytes_read, &error) != G_IO_STATUS_NORMAL) {
        if (error) {
            g_debug("Error reading from inotify: %s", error->message);
            g_error_free(error);
        }
        return TRUE;
    }

    /* Process events */
    while (offset < bytes_read) {
        event = (struct inotify_event *)(buf + offset);

        if (event->mask & IN_IGNORED) {
            offset += sizeof(struct inotify_event) + event->len;
            continue;
        }

        if (event->mask & FILE_REMOVE_MASK) {
            g_debug("Device or file removal detected, exiting");
            if (monitor_loop)
                g_main_loop_quit(monitor_loop);
            return FALSE;
        }

        if (event->mask & FILE_MODIFY_MASK) {
            g_debug("NVRAM change detected, writing immediately");
            write_nvram(nvram_filename);
        }

        offset += sizeof(struct inotify_event) + event->len;
    }

    return TRUE;
}

static gboolean
on_shutdown_signal(gpointer user_data)
{
    g_debug("Shutdown signal received in monitor thread");
    if (monitor_loop)
        g_main_loop_quit(monitor_loop);
    return FALSE;
}

void
wmt_start_monitor()
{
    int inot_fd = -1;
    int dev_wd = -1;
    int nvram_wd = -1;
    struct stat stat_buf;
    GIOChannel *inotify_channel = NULL;
    guint inotify_source_id = 0;
    char nvram_filename[BUF_SIZE] = {0};

    g_debug("Waiting for device %s to be accessible", WIFI_LOADER_DEV);
    while (access(WIFI_LOADER_DEV, R_OK | W_OK) < 0) {
        usleep(100000);
        g_debug("Still waiting for device...");
        if (g_atomic_int_get(&wmt_shutdown_flag))
            return;
    }
    g_debug("Device is now accessible");

    if (stat(WIFI_LOADER_DEV, &stat_buf) == -1) {
        g_debug("stat on %s failed: %s", WIFI_LOADER_DEV, strerror(errno));
        return;
    }

    if (!S_ISCHR(stat_buf.st_mode)) {
        g_debug("%s is not a char device", WIFI_LOADER_DEV);
        return;
    }

    /* Create our own context for this thread */
    monitor_context = g_main_context_new();
    monitor_loop = g_main_loop_new(monitor_context, FALSE);

    /* Push context for this thread */
    g_main_context_push_thread_default(monitor_context);

    inot_fd = inotify_init();
    if (inot_fd < 0) {
        g_debug("inotify_init failed: %s", strerror(errno));
        goto cleanup;
    }

    /* Make inotify fd non-blocking */
    int flags = fcntl(inot_fd, F_GETFL);
    fcntl(inot_fd, F_SETFL, flags | O_NONBLOCK);

    dev_wd = inotify_add_watch(inot_fd, WIFI_LOADER_DEV, FILE_REMOVE_MASK);
    if (dev_wd < 0)
        g_debug("Failed to add device watch: %s", strerror(errno));
    else
        g_debug("Device watch added successfully");

    memset(nvram_filename, 0, sizeof(nvram_filename));
    snprintf(nvram_filename, sizeof(nvram_filename), "%s/", WIFI_NVRAM_PATH);
    get_custom_nvram_file_name(nvram_filename);

    g_debug("Waiting for NVRAM file to be readable");
    while (access(nvram_filename, R_OK) < 0) {
        g_debug("NVRAM file not yet accessible, waiting...");
        sleep(1);
        if (g_atomic_int_get(&wmt_shutdown_flag))
            goto cleanup;
    }

    g_debug("NVRAM file is now accessible");

    nvram_wd = inotify_add_watch(inot_fd, nvram_filename, WATCH_FILE_MASK);
    if (nvram_wd < 0)
        g_debug("Failed to add NVRAM watch: %s", strerror(errno));
    else
        g_debug("NVRAM watch added successfully");

    /* Create GIOChannel for inotify */
    inotify_channel = g_io_channel_unix_new(inot_fd);
    g_io_channel_set_encoding(inotify_channel, NULL, NULL);
    g_io_channel_set_buffered(inotify_channel, FALSE);

    /* Add inotify source to the event loop */
    inotify_source_id = g_io_add_watch(inotify_channel,
                                       G_IO_IN | G_IO_HUP | G_IO_ERR,
                                       on_inotify_event,
                                       g_strdup(nvram_filename));

    /* Write initial NVRAM */
    write_nvram(nvram_filename);

    g_debug("Starting GLib main loop for monitoring");

    g_main_loop_run(monitor_loop);

cleanup:
    g_debug("Monitor loop exited, cleaning up");

    /* Remove sources */
    if (inotify_source_id > 0)
        g_source_remove(inotify_source_id);

    /* Clean up inotify watches */
    if (dev_wd > 0) {
        g_debug("Removing device watch");
        inotify_rm_watch(inot_fd, dev_wd);
    }

    if (nvram_wd > 0) {
        g_debug("Removing NVRAM watch");
        inotify_rm_watch(inot_fd, nvram_wd);
    }

    /* Clean up GIO channel */
    if (inotify_channel) {
        g_io_channel_shutdown(inotify_channel, FALSE, NULL);
        g_io_channel_unref(inotify_channel);
    }

    if (inot_fd >= 0)
        close(inot_fd);

    /* Pop thread context */
    g_main_context_pop_thread_default(monitor_context);

    /* Clean up context and loop */
    if (monitor_loop) {
        g_main_loop_unref(monitor_loop);
        monitor_loop = NULL;
    }

    if (monitor_context) {
        g_main_context_unref(monitor_context);
        monitor_context = NULL;
    }
}

void
wmt_signal_monitor_shutdown(void)
{
    g_debug("Signaling monitor thread to shutdown");

    if (monitor_context) {
        /* Schedule shutdown callback in monitor thread's context */
        GSource *idle_source = g_idle_source_new();
        g_source_set_callback(idle_source, on_shutdown_signal, NULL, NULL);
        g_source_attach(idle_source, monitor_context);
        g_source_unref(idle_source);
    }
}
