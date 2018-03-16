#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Endless Mobile, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301  USA

"""Integration tests for the eos-paygd process."""

import dbusmock
import os
import subprocess
import sysconfig
import unittest

import taptestrunner


class TestEosPaygd(dbusmock.DBusTestCase):
    """Integration test for running eos-paygd.

    This can be run when installed or uninstalled. When uninstalled, it
    requires G_TEST_BUILDDIR and G_TEST_SRCDIR to be set. It can run as any
    user, although running as root will result in most of the tests being
    skipped, as eos-paygd1 aborts when run as root on principle of least
    privilege.

    The idea with this test harness is to simulate simple integration
    situations for eos-paygd1, rather than to test any of the core code
    in depth. Unit tests exist for that.
    """

    @classmethod
    def setUpClass(klass):
        klass.start_system_bus()

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        if 'G_TEST_BUILDDIR' in os.environ:
            self.__eos_paygd = \
                os.path.join(os.environ['G_TEST_BUILDDIR'], '..', 'eos-paygd1')
        else:
            arch = sysconfig.get_config_var('multiarchsubdir').strip('/')
            self.__eos_paygd = os.path.join('/', 'lib', arch, 'eos-paygd1')

    @unittest.skipIf(os.geteuid() != 0, "Must be run as root")
    def test_abort_if_root(self):
        """Test the daemon exits immediately if run as root."""
        info = subprocess.run([self.__eos_paygd],
                              timeout=self.timeout_seconds,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('This daemon must not be run as root', out)
        self.assertEqual(info.returncode, 3)  # ERROR_INVALID_ENVIRONMENT


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
