/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "ip.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>

gchar **
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

gboolean
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
        return TRUE;
    }

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags &= ~IFF_UP;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) == 0)
            g_debug("Interface %s brought down", interface_name);
    }

    g_debug("Interface %s prepared for removal", interface_name);
    success = TRUE;

    close(sock);
    return success;
}

gboolean
enable_wowlan_magic_packet(gboolean enable)
{
    struct nl_sock *sock = NULL;
    struct nl_msg *msg = NULL;
    int driver_id;
    int ifindex;
    int err;
    gboolean success = FALSE;

    ifindex = if_nametoindex("phy0");
    if (ifindex == 0) {
        g_debug("Failed to resolve phy0 to ifindex");
        return FALSE;
    }

    sock = nl_socket_alloc();
    if (!sock) {
        g_debug("Failed to allocate netlink socket");
        return FALSE;
    }

    if (genl_connect(sock) < 0) {
        g_debug("Failed to connect to generic netlink");
        goto cleanup;
    }

    driver_id = genl_ctrl_resolve(sock, "nl80211");
    if (driver_id < 0) {
        g_debug("nl80211 not found");
        goto cleanup;
    }

    msg = nlmsg_alloc();
    if (!msg) {
        g_debug("Failed to allocate netlink message");
        goto cleanup;
    }

    genlmsg_put(msg, 0, 0, driver_id, 0, 0, NL80211_CMD_SET_WOWLAN, 0);

    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

    struct nlattr *wowlan = nla_nest_start(msg, NL80211_ATTR_WOWLAN_TRIGGERS);
    if (!wowlan) {
        g_debug("Failed to create WOWLAN trigger nested attribute");
        goto cleanup;
    }

    if (enable)
        nla_put_flag(msg, NL80211_WOWLAN_TRIG_MAGIC_PKT);

    nla_nest_end(msg, wowlan);

    err = nl_send_auto(sock, msg);
    if (err < 0) {
        g_debug("Failed to send WoWLAN message: %s", nl_geterror(err));
        goto cleanup;
    }

    err = nl_wait_for_ack(sock);
    if (err < 0) {
        g_debug("Failed to get ack: %s", nl_geterror(err));
        goto cleanup;
    }

    g_debug("WoWLAN magic-packet %s successfully", enable ? "enabled" : "disabled");
    success = TRUE;

cleanup:
    if (msg)
        nlmsg_free(msg);
    if (sock)
        nl_socket_free(sock);

    return success;
}
