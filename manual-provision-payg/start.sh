#!/bin/bash
# start.sh payg provision.
#mount unmounted removable devices
udisksctl dump |
awk -F':\n' -v'RS=\n\n' '/[ \t]*HintAuto:[ \t]*true/&&/\.Filesystem:/{
                             print $1
}' |
while read dev
do
    udisksctl mount --object-path "${dev##*/UDisks2/}"
done
clear
echo "Provisioning payg..."
echo "Please wait."
device_id=$(python3 /var/eos-factory-test/code_gen.py)
echo "YOUR DEVICE ID IS $device_id. PLEASE WRITE THIS DOWN!"
rm -rf /var/eos-factory-test
echo ""
echo "Navigate the cursor here, click and Hit enter to shutdown"
read -s -n 1 key
if [[ $key = "" ]]; then
    poweroff
else
    echo $key
fi
