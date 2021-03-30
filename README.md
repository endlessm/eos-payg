eos-payg
========

eos-payg implements pay-as-you-go support for EOS, providing a way to verify
top-up codes and monitor the amount of time the computer has remaining before
the user next needs to top up its lease.

It provides a daemon to monitor the expiry time for the current code and to
verify new codes, a command line utility for generating new codes, and various
helper libraries. It also supports loading out-of-tree plugins, which implement
alternative code management backends.

All the library APIs are currently unstable and are likely to change wildly.

**NOTE**: eos-paygd has two modes it can run in: (1) when run from the
initramfs, it uses Phase 4+ PAYG security features, and assumes that it's
running from a payg image, and (2) when run from the primary root filesystem,
it does not use the advanced security features and assumes it's running on a
Phase 2 PAYG system or a non-PAYG system.

When running from the initramfs, eos-paygd is unkillable. systemd will leave
it running during the root pivot and shutdown. The reason this is safe, and
that a system shutdown while it's writing to non-volatile storage isn't
considered a risk is that all the non-volatile storage backends in use are
robust against a shutdown during write. This is done either through the
ability to rollback an interrupted write, or writes that are fully atomic.

Since it would be very difficult to enter a timecode at the same time as
a system shutdown (because the UI would not be accessible during shutdown),
the most likely interrupted operation would be time expiry. On the following
boot the time would still be past expiration time, and the system would lock
again.

Dependencies
============

 * gio-2.0 ≥ 2.54
 * glib-2.0 ≥ 2.54
 * gobject-2.0 ≥ 2.54
 * peas
 * systemd

Licensing
=========

With a couple of exceptions, all code in this project is dual-licensed under
MPL-2.0 and LGPL-2.1+. See debian/copyright and COPYING.* for more details.

Bugs
====

Bug reports and patches should be filed in GitHub.

Contact
=======

https://github.com/endlessm/eos-payg
