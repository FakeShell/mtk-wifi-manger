/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef DBUS_H
#define DBUS_H

#include "wmt.h"

#define DBUS_SERVICE_NAME "com.MediaTek.WiFiManager"
#define DBUS_OBJECT_PATH "/com/MediaTek/WiFiManager"
#define DBUS_INTERFACE_NAME "com.MediaTek.WiFiManager"

typedef struct {
    GDBusConnection *connection;
    guint registration_id;
    WiFiState current_state;
} DBusService;

extern DBusService *g_dbus_service;

/**
 * Initialize the D-Bus service and register it on the system bus.
 *
 * @param service  Pointer to DBusService structure to initialize.
 * @param error    Location to store error information.
 * @return         TRUE on success, FALSE on failure.
 */
gboolean
dbus_service_init(DBusService *service, GError **error);

/**
 * Cleanup the D-Bus service and release resources.
 *
 * @param service  Pointer to DBusService structure to cleanup.
 */
void
dbus_service_cleanup(DBusService *service);

/**
 * Emit StateChanged signal with the new WiFi state.
 *
 * @param service    Pointer to DBusService structure.
 * @param new_state  New WiFi state to broadcast.
 */
void
dbus_emit_state_changed(DBusService *service, WiFiState new_state);

/**
 * Get the current WiFi state.
 *
 * @param service  Pointer to DBusService structure.
 * @return         Current WiFi state.
 */
WiFiState
dbus_get_current_state(DBusService *service);

/**
 * Handle D-Bus method calls.
 *
 * @param connection  D-Bus connection.
 * @param sender      Sender's unique name.
 * @param object_path Object path.
 * @param interface_name Interface name.
 * @param method_name Method name.
 * @param parameters  Method parameters.
 * @param invocation  Method invocation context.
 * @param user_data   User data (DBusService pointer).
 */
void
handle_method_call(GDBusConnection *connection,
                   const gchar *sender,
                   const gchar *object_path,
                   const gchar *interface_name,
                   const gchar *method_name,
                   GVariant *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer user_data);

/**
 * Handle D-Bus property get requests.
 *
 * @param connection     D-Bus connection.
 * @param sender         Sender's unique name.
 * @param object_path    Object path.
 * @param interface_name Interface name.
 * @param property_name  Property name.
 * @param error          Location to store error information.
 * @param user_data      User data (DBusService pointer).
 * @return               Property value as GVariant.
 */
GVariant *
handle_get_property(GDBusConnection *connection,
                    const gchar *sender,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *property_name,
                    GError **error,
                    gpointer user_data);

/**
 * Handle D-Bus property set requests.
 *
 * @param connection     D-Bus connection.
 * @param sender         Sender's unique name.
 * @param object_path    Object path.
 * @param interface_name Interface name.
 * @param property_name  Property name.
 * @param value          New property value.
 * @param error          Location to store error information.
 * @param user_data      User data (DBusService pointer).
 * @return               TRUE on success, FALSE on failure.
 */
gboolean
handle_set_property(GDBusConnection *connection,
                    const gchar *sender,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *property_name,
                    GVariant *value,
                    GError **error,
                    gpointer user_data);

extern DBusService *g_dbus_service;

#endif /* DBUS_H */
