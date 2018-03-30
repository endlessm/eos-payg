#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Endless Mobile, Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at https://mozilla.org/MPL/2.0/.
#
# Alternatively, the contents of this file may be used under the terms of the
# GNU Lesser General Public License Version 2.1 or later (the "LGPL"), in
# which case the provisions of the LGPL are applicable instead of those above.
# If you wish to allow use of your version of this file only under the terms
# of the LGPL, and not to allow others to use your version of this file under
# the terms of the MPL, indicate your decision by deleting the provisions
# above and replace them with the notice and other provisions required by the
# LGPL. If you do not delete the provisions above, a recipient may use your
# version of this file under the terms of either the MPL or the LGPL.

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
        p = subprocess.Popen([self.__eos_paygd],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
        out, _ = p.communicate(timeout=self.timeout_seconds)
        rc = p.returncode

        out = out.decode('utf-8').strip()
        self.assertIn('This daemon must not be run as root', out)
        self.assertEqual(rc, 3)  # ERROR_INVALID_ENVIRONMENT


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
