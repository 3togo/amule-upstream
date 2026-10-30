# This file is part of the aMule Project.
# Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
# Licensed under the GNU General Public License, version 2 or later.
"""Production aMule adapter + real libwakeguard against an isolated D-Bus."""
import os
import selectors
import subprocess
import sys
import threading
import time
import unittest
from gi.repository import Gio, GLib

PROBE = sys.argv.pop(1)
os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = os.environ['DBUS_SESSION_BUS_ADDRESS']
XML = '''<node><interface name="org.freedesktop.login1.Manager">
<method name="Inhibit"><arg type="s" direction="in"/><arg type="s" direction="in"/>
<arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="h" direction="out"/>
</method></interface><interface name="org.freedesktop.portal.Inhibit">
<method name="Inhibit"><arg type="s" direction="in"/><arg type="u" direction="in"/>
<arg type="a{sv}" direction="in"/><arg type="o" direction="out"/>
</method></interface></node>'''


def eventually(condition):
    end = time.monotonic() + 5
    while time.monotonic() < end:
        if condition():
            return True
        time.sleep(.005)
    return False


def released(fd):
    try:
        return os.read(fd, 1) == b''
    except BlockingIOError:
        return False


class Pipeline(unittest.TestCase):
    def setUp(self):
        self.ready = threading.Event()
        self.calls = []
        self.readers = []
        self.pending = []
        self.block = False
        self.deny_portal = False
        self.clients = []
        self.worker = threading.Thread(target=self.server)
        self.worker.start()
        self.assertTrue(self.ready.wait(5))

    def server(self):
        context = GLib.MainContext.new()
        context.push_thread_default()
        self.bus = Gio.DBusConnection.new_for_address_sync(
            os.environ['DBUS_SESSION_BUS_ADDRESS'],
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None, None)
        self.bus.set_exit_on_close(False)
        self.loop = GLib.MainLoop.new(context, False)
        interfaces = Gio.DBusNodeInfo.new_for_xml(XML).interfaces
        self.bus.register_object('/org/freedesktop/login1', interfaces[0], self.inhibit, None, None)
        self.bus.register_object('/org/freedesktop/portal/desktop', interfaces[1], self.inhibit, None, None)
        for name in ('org.freedesktop.login1', 'org.freedesktop.portal.Desktop'):
            self.bus_call('RequestName', GLib.Variant('(su)', (name, 0)), '(u)')
        self.ready.set()
        self.loop.run()
        self.bus.close_sync(None)
        context.pop_thread_default()

    def bus_call(self, method, parameters, response):
        return self.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', method, parameters, GLib.VariantType.new(response),
            Gio.DBusCallFlags.NONE, 2000, None).unpack()

    def owner_present(self, owner):
        return self.bus_call('NameHasOwner', GLib.Variant('(s)', (owner,)), '(b)')[0]

    def complete_fd(self, invocation):
        reader, writer = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
        self.readers.append(reader)
        fds = Gio.UnixFDList.new()
        index = fds.append(writer)
        os.close(writer)
        invocation.return_value_with_unix_fd_list(GLib.Variant('(h)', (index,)), fds)

    def inhibit(self, connection, sender, path, interface, method, parameters, invocation):
        self.calls.append((interface, parameters.unpack(), sender))
        if interface == 'org.freedesktop.portal.Inhibit':
            if self.deny_portal:
                invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied', 'Test denial')
                return
            handle = '/org/freedesktop/portal/desktop/request/test/amule'
            connection.emit_signal(sender, handle, 'org.freedesktop.portal.Request', 'Response',
                GLib.Variant('(ua{sv})', (0, {})))
            invocation.return_value(GLib.Variant('(o)', (handle,)))
        elif self.block:
            self.pending.append(invocation)
        else:
            self.complete_fd(invocation)

    def read(self, client):
        with selectors.DefaultSelector() as poll:
            poll.register(client.stdout, selectors.EVENT_READ)
            self.assertTrue(poll.select(6), 'probe command blocked')
        return client.stdout.readline().strip()

    def start(self, policy, desktop_session=False):
        env = os.environ.copy()
        env['XDG_CURRENT_DESKTOP'] = 'XFCE' if desktop_session else ''
        client = subprocess.Popen([PROBE, policy], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        self.clients.append(client)
        self.assertEqual(self.read(client), 'READY')
        return client

    def command(self, client, command):
        client.stdin.write(command + '\n')
        client.stdin.flush()
        self.assertEqual(self.read(client), 'STOPPED' if command == 'quit' else 'ACK')

    def tearDown(self):
        failures = []
        try:
            for client in self.clients:
                if client.stdin:
                    client.stdin.close()
                    client.stdin = None
                try:
                    out, error = client.communicate(timeout=6)
                except subprocess.TimeoutExpired:
                    client.kill()
                    out, error = client.communicate(timeout=3)
                    failures.append('probe failed to shut down')
                if client.returncode:
                    failures.append(error)
            self.assertFalse(failures, '\n'.join(failures))
        finally:
            self.loop.quit()
            self.worker.join(5)
            for reader in self.readers:
                os.close(reader)

    def test_daemon_ticks_metadata_and_release(self):
        client = self.start('daemon')
        self.command(client, 'on')
        self.assertTrue(eventually(lambda: len(self.readers) == 1))
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.calls[0][1], ('idle', 'aMule', 'Transferring files', 'block'))
        self.assertFalse(released(self.readers[0]))
        self.command(client, 'off')
        self.assertTrue(eventually(lambda: released(self.readers[0])))

    def test_daemon_in_desktop_session_uses_portal(self):
        client = self.start('daemon', desktop_session=True)
        self.command(client, 'on')
        self.assertTrue(eventually(lambda: len(self.calls) == 1))
        self.assertEqual(self.calls[0][0], 'org.freedesktop.portal.Inhibit')
        self.assertEqual(self.calls[0][1][1], 4)
        self.command(client, 'off')
        self.assertTrue(eventually(lambda: not self.owner_present(self.calls[0][2])))

    def test_desktop_portal_and_release(self):
        client = self.start('desktop')
        self.command(client, 'on')
        self.assertTrue(eventually(lambda: len(self.calls) == 1))
        self.assertEqual(self.calls[0][0], 'org.freedesktop.portal.Inhibit')
        self.assertEqual(self.calls[0][1][1], 4)
        self.assertEqual(self.calls[0][1][2]['reason'], 'Transferring files')
        self.command(client, 'off')
        self.assertTrue(eventually(lambda: not self.owner_present(self.calls[0][2])))

    def test_desktop_logind_suspend_fallback(self):
        self.deny_portal = True
        client = self.start('desktop')
        self.command(client, 'on')
        self.assertTrue(eventually(lambda: len(self.readers) == 1))
        self.assertEqual(self.calls[-1][1], ('sleep:idle', 'aMule', 'Transferring files', 'block'))
        self.command(client, 'quit')
        self.assertTrue(eventually(lambda: released(self.readers[0])))

    def test_disable_during_acquisition_discards_result(self):
        self.block = True
        client = self.start('daemon')
        start = time.monotonic()
        self.command(client, 'on')
        self.assertLess(time.monotonic() - start, 1.5)
        self.assertTrue(eventually(lambda: len(self.pending) == 1))
        start = time.monotonic()
        self.command(client, 'off')
        self.assertLess(time.monotonic() - start, 1.5)
        self.complete_fd(self.pending.pop())
        self.assertTrue(eventually(lambda: released(self.readers[0])))


if __name__ == '__main__':
    unittest.main(verbosity=2)
