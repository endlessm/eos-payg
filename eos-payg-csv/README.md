eos-payg-csv
============
Usage:
    eos-payg-csv ACCT_KEY_CSV_FILE

Read per-device `DEVICE_ID` and Pay As You Go private keys from
`ACCT_KEY_CSV_FILE` and generate time codes for several time periods per key.

For each `DEVICE_ID`, the last corresponding key that appears in
`ACCT_KEY_CSV_FILE` will be used to generate these unlock codes and the output
will be `DEVICE_ID.csv`. Earlier keys are ignored in case a given computer is
provisioned multiple times as only the last key will be in place. This is done
to add resiliency to the provisioning process.

`ACCT_KEY_CSV_FILE` File format
===============================
The expected format of this file is a CSV with the following header row
and one or more device rows:

header row
----------
```
device_id,code1,code2,code3,key
```

device row
----------
```
DEVICE_ID,CODE_1,CODE_2,CODE_3,"KEY"
```

where:

* `DEVICE_ID`: 8 hex digit device ID that must be stable for a given device
* `CODE_N`:    a temporary code for the given key
* `KEY`:       a private key to be used to generate time codes for this device.
               This may be quoted with double quotes and span multiple lines.

There may be multiple entries for a given `DEVICE_ID`. In case that
happens, only the last row will be considered and used to generate time
codes.

For a sample valid input file, see `tests/valid.csv`.
