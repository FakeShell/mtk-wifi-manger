#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>

import dbus
import dbus.mainloop.glib
from gi.repository import GLib
import threading
import sys
import time
import signal

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

DBUS_SERVICE_NAME = "com.MediaTek.WiFiManager"
DBUS_OBJECT_PATH = "/com/MediaTek/WiFiManager"
DBUS_INTERFACE_NAME = "com.MediaTek.WiFiManager"

WIFI_STATES = {
    1: "AP",
    2: "P2P",
    3: "DUAL_AP",
    4: "DUAL_P2P",
    5: "ON",
    6: "OFF"
}

STATE_NAMES = {v: k for k, v in WIFI_STATES.items()}

class WiFiManagerAPI:
    def __init__(self, bus):
        self.bus = bus
        self.service_name = DBUS_SERVICE_NAME
        self.object_path = DBUS_OBJECT_PATH
        self.interface_name = DBUS_INTERFACE_NAME

        try:
            self.proxy_object = bus.get_object(self.service_name, self.object_path)
            self.interface = dbus.Interface(self.proxy_object, self.interface_name)
            self.properties = dbus.Interface(self.proxy_object, 'org.freedesktop.DBus.Properties')
            print(f"Connected to {self.service_name}")
        except dbus.exceptions.DBusException as e:
            print(f"Error connecting to {self.interface_name}: {e}")
            raise

        self.register_signals()

    def register_signals(self):
        self.bus.add_signal_receiver(
            self.on_state_changed,
            dbus_interface=self.interface_name,
            signal_name="StateChanged"
        )

    def on_state_changed(self, state):
        state_name = WIFI_STATES.get(state, f"UNKNOWN ({state})")
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        print(f"[{timestamp}] State changed to: {state} ({state_name})")

    def get_property(self, name):
        try:
            return self.properties.Get(self.interface_name, name)
        except dbus.exceptions.DBusException as e:
            print(f"Error getting property {name}: {e}")
            return None

    def set_state(self, state_value):
        try:
            self.interface.SetState(dbus.UInt32(state_value))
            return True
        except dbus.exceptions.DBusException as e:
            print(f"Set state error: {e}")
            return False

    def refresh_p2p(self):
        try:
            result = self.interface.WpaRefreshP2P()
            return bool(result)
        except dbus.exceptions.DBusException as e:
            print(f"Refresh P2P error: {e}")
            return False

    def get_current_state(self):
        """Get current WiFi state"""
        state = self.get_property('State')
        if state is not None:
            state_name = WIFI_STATES.get(state, f"UNKNOWN ({state})")
            return state, state_name
        return None, None

    def print_properties(self):
        """Print current WiFi properties"""
        try:
            state, state_name = self.get_current_state()

            print("\n=== WiFi Manager Properties ===")
            if state is not None:
                print(f"Current State: {state} ({state_name})")
            else:
                print("Current State: Unable to retrieve")
            print("==============================\n")
        except Exception as e:
            print(f"Error printing properties: {e}")

    def list_valid_states(self):
        """List all valid WiFi states"""
        print("\n=== Valid WiFi States ===")
        for state_num, state_name in WIFI_STATES.items():
            print(f"{state_num}. {state_name}")
        print("=======================\n")

class WiFiManagerClient:
    def __init__(self):
        self.bus = dbus.SystemBus()
        self.mainloop = GLib.MainLoop()

        try:
            self.wifi_manager = WiFiManagerAPI(self.bus)
            print("WiFi Manager API connected")
        except Exception as e:
            self.wifi_manager = None
            print(f"WiFi Manager API not available: {e}")
            sys.exit(1)

        self.mainloop_thread = threading.Thread(target=self.mainloop.run)
        self.mainloop_thread.daemon = True
        self.mainloop_thread.start()

        signal.signal(signal.SIGINT, self.signal_handler)

    def signal_handler(self, signum, frame):
        print("\nExiting...")
        self.mainloop.quit()
        sys.exit(0)

    def run_interactive(self):
        try:
            if self.wifi_manager:
                self.wifi_manager.print_properties()
                print("StateChanged signals will be displayed automatically")

            while True:
                choice = self.print_menu()
                self.handle_menu_choice(choice)
        except KeyboardInterrupt:
            print("\nExiting...")
        finally:
            self.mainloop.quit()

    def print_menu(self):
        print("\n=== WiFi Manager Actions ===")
        print("1. View properties")
        print("2. Set WiFi state")
        print("3. Refresh P2P configuration")
        print("4. List valid states")
        print("0. Exit")
        print("==========================")
        return input("Select option: ")

    def handle_menu_choice(self, choice):
        if choice == '1':
            if self.wifi_manager:
                self.wifi_manager.print_properties()
            else:
                print("WiFi Manager API not available")
        elif choice == '2':
            self.handle_set_state()
        elif choice == '3':
            self.handle_refresh_p2p()
        elif choice == '4':
            if self.wifi_manager:
                self.wifi_manager.list_valid_states()
            else:
                print("WiFi Manager API not available")
        elif choice == '0':
            print("Exiting...")
            self.mainloop.quit()
            sys.exit(0)
        else:
            print("Invalid option")

    def handle_get_state(self):
        if not self.wifi_manager:
            print("WiFi Manager API not available")
            return

        state, state_name = self.wifi_manager.get_current_state()
        if state is not None:
            print(f"Current WiFi state: {state} ({state_name})")
        else:
            print("Failed to get current state")

    def handle_set_state(self):
        if not self.wifi_manager:
            print("WiFi Manager API not available")
            return

        self.wifi_manager.list_valid_states()

        selection = input("Enter state number (1-6) or name: ")

        try:
            state_num = int(selection)
            if state_num not in WIFI_STATES:
                print(f"Invalid state number: {state_num}")
                return
        except ValueError:
            state_upper = selection.upper()
            if state_upper in STATE_NAMES:
                state_num = STATE_NAMES[state_upper]
            else:
                print(f"Invalid state name: {selection}")
                return

        print(f"Setting WiFi state to {state_num} ({WIFI_STATES[state_num]})...")
        success = self.wifi_manager.set_state(state_num)
        print(f"State {'set successfully' if success else 'set failed'}")

    def handle_refresh_p2p(self):
        if not self.wifi_manager:
            print("WiFi Manager API not available")
            return

        print("Refreshing P2P configuration...")
        success = self.wifi_manager.refresh_p2p()
        print(f"P2P refresh {'successful' if success else 'failed'}")

def main():
    print("MediaTek WiFi Manager D-Bus Client")
    client = WiFiManagerClient()
    client.run_interactive()

if __name__ == "__main__":
    main()
