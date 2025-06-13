/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "dbus.h"
#include "wmt.h"
#include "wpa.h"

DBusService *g_dbus_service = NULL;

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='com.MediaTek.WiFiManager'>"
    "    <method name='SetState'>"
    "      <arg type='u' name='state' direction='in'/>"
    "    </method>"
    "    <method name='WpaRefreshP2P'>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg type='u' name='state'/>"
    "    </signal>"
    "    <property name='State' type='u' access='read'/>"
    "  </interface>"
    "</node>";

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    handle_get_property,
    handle_set_property
};

void
handle_method_call(GDBusConnection *connection,
                   const gchar *sender,
                   const gchar *object_path,
                   const gchar *interface_name,
                   const gchar *method_name,
                   GVariant *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer user_data)
{
    DBusService *service = (DBusService *)user_data;

    if (g_strcmp0(method_name, "SetState") == 0) {
        guint32 new_state_value;
        g_variant_get(parameters, "(u)", &new_state_value);

        g_debug("SetState called with state: %u", new_state_value);

        if (new_state_value != WIFI_STATE_AP &&
            new_state_value != WIFI_STATE_P2P &&
            new_state_value != WIFI_STATE_DUAL_AP &&
            new_state_value != WIFI_STATE_DUAL_P2P &&
            new_state_value != WIFI_STATE_ON &&
            new_state_value != WIFI_STATE_OFF) {
            g_debug("Invalid state value: %u", new_state_value);
            g_dbus_method_invocation_return_error(invocation,
                                                  G_DBUS_ERROR,
                                                  G_DBUS_ERROR_INVALID_ARGS,
                                                  "Invalid state value. Must be 1 (AP), 2 (P2P), 3 (DUAL_AP), 4 (DUAL_P2P), 5 (ON), or 6 (OFF)");
            return;
        }

        WiFiState new_state = (WiFiState)new_state_value;

        if (service->current_state == new_state) {
            g_debug("State is already set to %d, no change needed", new_state);
            g_dbus_method_invocation_return_value(invocation, NULL);
            return;
        }

        WiFiState old_state = service->current_state;

        /* Apply the state change via WMT */
        if (wmt_set_state(new_state) == 0) {
            service->current_state = new_state;

            g_debug("State changed from %d to %d", old_state, new_state);

            g_dbus_method_invocation_return_value(invocation, NULL);
        } else {
            g_debug("Failed to set state %d via WMT", new_state);
            g_dbus_method_invocation_return_error(invocation,
                                                  G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "Failed to apply state change to WiFi hardware");
        }
    } else if (g_strcmp0(method_name, "WpaRefreshP2P") == 0) {
        g_debug("WpaRefreshP2P called");

        gboolean success = wpa_refresh_p2p();

        g_debug("WpaRefreshP2P completed with success: %s", success ? "true" : "false");

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", success));
    } else {
        g_debug("Unknown method: %s", method_name);
        g_dbus_method_invocation_return_error(invocation,
                                              G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method");
    }
}

GVariant *
handle_get_property(GDBusConnection *connection,
                    const gchar *sender,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *property_name,
                    GError **error,
                    gpointer user_data)
{
    DBusService *service = (DBusService *)user_data;

    if (g_strcmp0(property_name, "State") == 0) {
        g_debug("Property 'State' requested, returning: %d", service->current_state);
        return g_variant_new_uint32((guint32)service->current_state);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s", property_name);
    return NULL;
}

gboolean
handle_set_property(GDBusConnection *connection,
                    const gchar *sender,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *property_name,
                    GVariant *value,
                    GError **error,
                    gpointer user_data)
{
    /* State property is read-only */
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY,
                "Property '%s' is read-only", property_name);
    return FALSE;
}

void
dbus_emit_state_changed(DBusService *service, WiFiState new_state)
{
    GError *error = NULL;

    g_debug("Emitting StateChanged signal with state: %d", new_state);

    /* Set current_state again in case we are called from somewhere other than handle_method_call */
    service->current_state = new_state;

    gboolean result = g_dbus_connection_emit_signal(service->connection,
                                                    NULL,
                                                    DBUS_OBJECT_PATH,
                                                    DBUS_INTERFACE_NAME,
                                                    "StateChanged",
                                                    g_variant_new("(u)", (guint32)new_state),
                                                    &error);

    if (!result) {
        g_debug("Failed to emit StateChanged signal: %s", error->message);
        g_error_free(error);
    } else {
        g_debug("StateChanged signal emitted successfully");
    }
}

WiFiState
dbus_get_current_state(DBusService *service)
{
    return service->current_state;
}

gboolean
dbus_service_init(DBusService *service, GError **error)
{
    GDBusNodeInfo *introspection_data;
    GError *local_error = NULL;

    g_debug("Initializing D-Bus service");

    /* Initialize service structure */
    service->current_state = WIFI_STATE_ON; /* Default to ON mode */
    service->connection = NULL;
    service->registration_id = 0;

    service->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &local_error);
    if (!service->connection) {
        g_debug("Failed to connect to system bus: %s", local_error->message);
        g_propagate_error(error, local_error);
        return FALSE;
    }

    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &local_error);
    if (!introspection_data) {
        g_debug("Failed to parse introspection XML: %s", local_error->message);
        g_propagate_error(error, local_error);
        return FALSE;
    }

    service->registration_id = g_dbus_connection_register_object(service->connection,
                                                                 DBUS_OBJECT_PATH,
                                                                 introspection_data->interfaces[0],
                                                                 &interface_vtable,
                                                                 service,  /* user_data */
                                                                 NULL,     /* user_data_free_func */
                                                                 &local_error);

    g_dbus_node_info_unref(introspection_data);

    if (service->registration_id == 0) {
        g_debug("Failed to register object: %s", local_error->message);
        g_propagate_error(error, local_error);
        return FALSE;
    }

    g_debug("Object registered with ID: %u", service->registration_id);

    GVariant *result = g_dbus_connection_call_sync(service->connection,
                                                   "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus",
                                                   "RequestName",
                                                   g_variant_new("(su)", DBUS_SERVICE_NAME, 0),
                                                   G_VARIANT_TYPE("(u)"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1,
                                                   NULL,
                                                   &local_error);

    if (!result) {
        g_debug("Failed to request service name: %s", local_error->message);
        g_propagate_error(error, local_error);
        return FALSE;
    }

    guint32 request_result;
    g_variant_get(result, "(u)", &request_result);
    g_variant_unref(result);

    if (request_result != 1) { /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */
        g_debug("Failed to acquire service name, result: %u", request_result);
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "Failed to acquire service name");
        return FALSE;
    }

    g_debug("Service name '%s' acquired successfully", DBUS_SERVICE_NAME);

    return TRUE;
}

void
dbus_service_cleanup(DBusService *service)
{
    if (!service)
        return;

    g_debug("Cleaning up D-Bus service");

    if (service->registration_id > 0) {
        g_dbus_connection_unregister_object(service->connection, service->registration_id);
        service->registration_id = 0;
    }

    if (service->connection) {
        g_object_unref(service->connection);
        service->connection = NULL;
    }
}
