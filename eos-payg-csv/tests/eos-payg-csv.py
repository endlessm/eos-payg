#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright © 2019 Endless Mobile, Inc.
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

"""Integration tests for the eos-payg-csv utility."""

import csv
import os
import shutil
import subprocess
import tempfile
import unittest

import taptestrunner


class TestEosPaygCsv(unittest.TestCase):
    """Integration test for running eos-payg-csv.

    This can be run when installed or uninstalled. When uninstalled, it
    requires G_TEST_BUILDDIR and G_TEST_SRCDIR to be set.

    The idea with this test harness is to test the eos-payg-csv utility,
    its handling of command line arguments, its exit statuses, and basic
    sanity of its generated output.
    """

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        self.tmpdir = tempfile.mkdtemp()
        os.chdir(self.tmpdir)
        print('tmpdir:', self.tmpdir)
	# look for the program in the source dir since it's not built
        if 'G_TEST_SRCDIR' in os.environ:
            self.__eos_payg_csv = \
                os.path.join(os.environ['G_TEST_SRCDIR'], '..', 'eos-payg-csv')
            self.__datadir = os.environ['G_TEST_SRCDIR']
        else:
            self.__eos_payg_csv = os.path.join('/', 'usr', 'bin',
                                               'eos-payg-csv')
            self.__datadir = os.path.join('/', 'usr', 'share', 'installed-tests',
                                          'eos-payg-csv')
        print('eos-payg-csv:', self.__eos_payg_csv)
        print('data dir:', self.__datadir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def createKey(self, contents=None):
        if not contents:
            contents = 'this is a key with at least 64 bytes of content ' + \
                       'otherwise we get an error'
        with open('key', 'w') as key_file:
            key_file.write(contents)
        return 'key'

    def runCsv(self, *args):
        argv = [self.__eos_payg_csv]
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

    def test_help(self):
        """Test getting help."""
        info = self.runCsv('-h')
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('device_id,code1,code2,code3,key\n', out)
        self.assertIn('DEVICE_ID_1,CODE_1,CODE_2,CODE_2,KEY\n', out)

    def test_help_no_args(self):
        """Test getting help if no arguments are provided."""
        info = self.runCsv()
        self.assertIsNot(info.returncode, 0)
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('usage:', out)
        self.assertIn('following arguments are required: ACCT_KEY_CSV_FILE', out)

    def test_missing_file(self):
        """Test error handling when passing an invalid filename."""
        info = self.runCsv('non/existing/file')
        out = info.stdout.decode('utf-8').strip()
        self.assertIn("failed to read key file non/existing/file", out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_csv_wrong_columns(self):
        """Test error handling when providing incorrectly-formatted file."""
        info = self.runCsv(os.path.join(self.__datadir, 'wrong-columns.csv'))
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Provided CSV invalid:\nexpected header', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def test_csv_short_key(self):
        """Test error handling when providing a CSV with a short key entry."""
        info = self.runCsv(os.path.join(self.__datadir, 'short-key.csv'))
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('Provided CSV invalid: minimum key length 64 bytes but '
                      'CSV contains a key of 12 bytes', out)
        self.assertEqual(info.returncode, 1)  # EXIT_INVALID_OPTIONS

    def assert_csv_valid(self, filename):
        cells = []
        with open(filename, 'r') as codes_generated_file:
            for row in csv.reader(codes_generated_file, quotechar='"'):
                cells.append(row)

        height = len(cells)
        width = len(cells[0])
        # ensure the CSV is rectangular as a basic requirement
        for row in cells:
            self.assertEqual(len(row), width)

        # as a proxy for ensuring the codes are in the right order, ensure that
        # they increase down and across. We don't have the logic available to
        # validate every code but, as of this writing, this characteristic
        # of generated codes appears to be true and catches an earlier bug where
        # codes were output in the wrong column (eg, so a 5-day code might
        # appear under the 30-day header, or infinite under the 1-day header --
        # either is a total disaster)

        # skip the first row (the headers)
        for i in range(1, height):
            for j in range(0, width):
                # skip the headers
                if i > 1:
                    self.assertTrue(cells[i-1][j] < cells[i][j])

                if j > 0:
                    self.assertTrue(cells[i][j-1] < cells[i][j])

                # ensure every time code starts (but does not end with) a single
                # quote to force Google Sheets to treat every cell as a string
                # literal, not a number (where leading zeros, which are
                # critical, be included in lower-number timecodes)
                self.assertTrue(cells[i][j].startswith("'"))
                self.assertFalse(cells[i][j].endswith("'"))

    def test_csv_valid(self):
        """Test proper operation of the script for a valid input file with two
        unique device_ids and multiple keys for a single device_id."""
        builddir = os.environ['G_TEST_BUILDDIR']
        info = self.runCsv('--eos-payg-generate',
                           os.path.join(builddir, '..', '..', 'eos-payg-generate',
                                        'eos-payg-generate-1'),
                           os.path.join(self.__datadir,
                                        'valid-2-keys-dupes.csv'))
        info.check_returncode()

        self.assert_csv_valid('3F0A1564.csv')
        self.assert_csv_valid('8AF5DA70.csv')

if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
