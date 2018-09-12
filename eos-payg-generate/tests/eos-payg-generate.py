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

"""Integration tests for the eos-payg-generate utility."""

import os
import shutil
import subprocess
import tempfile
import unittest

import taptestrunner


class TestEosPaygGenerate(unittest.TestCase):
    """Integration test for running eos-payg-generate.

    This can be run when installed or uninstalled. When uninstalled, it
    requires G_TEST_BUILDDIR and G_TEST_SRCDIR to be set.

    The idea with this test harness is to test the eos-payg-generate utility,
    its handling of command line arguments and its exit statuses; rather than
    to test any of the core code generation/verification code in depth. Unit
    tests exist for that.
    """

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        self.tmpdir = tempfile.mkdtemp()
        os.chdir(self.tmpdir)
        print('tmpdir:', self.tmpdir)
        if 'G_TEST_BUILDDIR' in os.environ:
            self.__eos_payg_generate = \
                os.path.join(os.environ['G_TEST_BUILDDIR'], '..',
                             'eos-payg-generate-1')
        else:
            self.__eos_payg_generate = os.path.join('/', 'usr', 'bin',
                                                    'eos-payg-generate-1')
        print('eos_payg_generate:', self.__eos_payg_generate)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def createKey(self, contents=None):
        if not contents:
            contents = 'this is a key with at least 64 bytes of content ' + \
                       'otherwise we get an error'
        with open('key', 'w') as key_file:
            key_file.write(contents)
        return 'key'

    def runGenerate(self, *args):
        argv = [self.__eos_payg_generate]
        argv.extend(args)
        print('Running:', argv)

        env = os.environ.copy()
        env['LC_ALL'] = 'C.UTF-8'
        print('Environment:', env)

        info = subprocess.run(argv, timeout=self.timeout_seconds,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,
                              env=env)
        print('Output:', info.stdout.decode('utf-8'))
        return info

    def test_list_periods(self):
        """Test listing periods."""
        info = self.runGenerate('-l')
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Available periods:\n', out)
        self.assertIn(' • 5d — 5 days\n', out)

    def test_list_periods_quiet(self):
        """Test listing periods with -q."""
        info = self.runGenerate('-l', '-q')
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertNotIn('Available periods:\n', out)
        self.assertNotIn(' • 5d — 5 days\n', out)
        self.assertIn('5d\n', out)

    def test_generate_with_counter(self):
        """Test generating a single code by specifying a counter."""
        info = self.runGenerate(self.createKey(), '1d', '5')
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertEqual('08433942', out)

    def test_generate_without_counter(self):
        """Test generating a whole set of codes by omitting a counter."""
        info = self.runGenerate(self.createKey(), '1d')
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()

        # Test that some arbitary counters are in there:
        self.assertIn('08433942\n', out)
        self.assertIn('10343286\n', out)

        # And check the number of codes generated overall (the terminal \n is
        # stripped, above)
        self.assertEqual(out.count('\n'), 256 - 1)

    def test_generate_invalid_period(self):
        """Test error handling when passing an invalid period."""
        info = self.runGenerate(self.createKey(), 'not really valid')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Invalid period ‘not really valid’', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_missing_period(self):
        """Test error handling when passing no period."""
        info = self.runGenerate(self.createKey())
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Option parsing failed: A KEY-FILENAME and PERIOD are '
                      'required', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_missing_key(self):
        """Test error handling when passing no key or period."""
        info = self.runGenerate()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Option parsing failed: A KEY-FILENAME and PERIOD are '
                      'required', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_too_many_options(self):
        """Test error handling when passing too many options."""
        info = self.runGenerate('key', '1d', '123', 'spurious option')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Option parsing failed: Too many arguments provided',
                      out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_invalid_argument(self):
        """Test error handling when passing an invalid argument."""
        info = self.runGenerate('--not-an-argument')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Option parsing failed: Unknown option '
                      '--not-an-argument', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_invalid_key(self):
        """Test error handling when passing an invalid key."""
        info = self.runGenerate('not a key', '1d')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('No such file or directory', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_invalid_counter(self):
        """Test error handling when passing an invalid counter."""
        info = self.runGenerate(self.createKey(), '1d', 'hello world')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('“hello world” is not an unsigned number', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_generate_short_key(self):
        """Test error handling when passing a key which is too short."""
        info = self.runGenerate(self.createKey('short'), '1d')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Key is too short (5 bytes); minimum length 64 bytes.', out)
        self.assertEqual(info.returncode, 2)  # EXIT_FAILED


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
