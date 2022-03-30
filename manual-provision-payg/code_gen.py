#!/usr/bin/python3
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gio
from gi.repository import Gtk
from gi.repository import GLib
import sys
import os
from os.path import expanduser
import string
import random
import textwrap
import json
import os.path
import hashlib
import textwrap
import struct
import hmac
import csv
import errno
from string import Template

key_code_file_name = "payg-test-codes"
device_id = ""
cwd = ""


def set_usb_path():
    for drive in enumerate_drives():
        usb_path_obj = get_usb_path(drive)
        if usb_path_obj["path"]:
            global cwd
            cwd = usb_path_obj["path"] + "/"
            break
    if not cwd:
        print(usb_path_obj["errors"], file=sys.stderr)


def enumerate_drives():
    DBUS_NAME = "org.freedesktop.UDisks2"
    DBUS_PATH = "/org/freedesktop/UDisks2"
    DBUS_INTERFACE = "org.freedesktop.DBus.ObjectManager"
    connection = Gio.bus_get_sync(Gio.BusType.SYSTEM)
    proxy = Gio.DBusProxy.new_sync(
        connection,
        Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES
        | Gio.DBusProxyFlags.DO_NOT_CONNECT_SIGNALS,
        info=None,
        name=DBUS_NAME,
        object_path=DBUS_PATH,
        interface_name=DBUS_INTERFACE,
    )
    managed_objects = proxy.call_sync(
        "GetManagedObjects", GLib.Variant("()", ()), Gio.DBusCallFlags.NONE, -1, None
    )
    # returns tuple with single object
    devices_object = managed_objects.unpack()[0]
    devices = []
    for key, value in devices_object.items():
        block_device_obj = None
        try:
            block_device_obj = value["org.freedesktop.UDisks2.Block"]
        except KeyError:
            block_device_obj = None
        if block_device_obj is not None:
            if (
                block_device_obj.get("IdUsage") == "filesystem"
                and not block_device_obj.get("HintSystem")
                and not block_device_obj.get("ReadOnly")
            ):
                devices.append(block_device_obj)

    return devices


def get_usb_path(drive):
    user = "root"
    root_folder = "/run/media/"
    usb_file_path = None
    errors = ""
    try:
        label = drive.get("IdLabel")
        uuid = drive.get("IdUUID")

        if label and len(label) > 0:
            path = os.path.join(root_folder, user, label)
            if os.path.isfile(path + "/Endless_Factory_Test.tar"):
                usb_file_path = path
            else:
                errors += "couldnt find " + path + "\n"
        else:
            path = os.path.join(root_folder, user, uuid)
            if os.path.isfile(path + "/Endless_Factory_Test.tar"):
                usb_file_path = path
            else:
                errors += "couldnt find " + path + "\n"
    except:
        # TODO: Add better exception handling.
        errors += "Error detecting USB details. \n"
    return {"path": usb_file_path, "errors": errors}


def password_generator(size=768, chars=string.ascii_letters + string.digits):
    # size is currently arbitrarily set to mimic output length from apg
    return textwrap.fill("".join(random.choice(chars) for i in range(size)))


def write_to_file(file_location, text):
    os.makedirs(os.path.dirname(file_location), exist_ok=True)
    with open(file_location, "w") as file:
        file.write(text)


def generate_codes(key, device_id):
    key_token_data = key_gen(key, device_id)
    write_to_csv(key_token_data)
    write_to_json(key_token_data)


def generate_key():
    key = password_generator()
    write_to_file(cwd + "generated-key", key)
    return key


def backup_file(file_to_backup, backup_path):
    # "/usr/local/share/eos-payg/key"
    file_available = os.path.isfile(file_to_backup)
    if file_available:
        with open(file_to_backup, "r") as f:
            key_contents = f.read()
            write_to_file(backup_path, key_contents)


def add_key():
    key_path_destination = "/usr/local/share/eos-payg/key"
    key_path_source = cwd + "generated-key"
    with open(key_path_source, "r") as f:
        generated_key_contents = f.read()
        write_to_file(key_path_destination, generated_key_contents)


def install_instructions():
    config_instructions = {}
    try:
        with open(cwd + "config" + ".json") as f:
            config_instructions = json.load(f)
    except FileNotFoundError:
        print("Please add instruction config file\n", file=sys.stderr)
        sys.exit(1)
    template_instruction_1 = Template(config_instructions.get("InstructionsLine1"))
    instruction_1 = template_instruction_1.substitute(device_id=device_id)
    instruction_2 = config_instructions.get("InstructionsLine2")
    payg_instructions = """[Pay As You Go]
    InstructionsLine1= %s
    InstructionsLine2= %s""" % (
        instruction_1,
        instruction_2,
    )
    backup_file(
        "/var/lib/eos-image-defaults/vendor-customer-support.ini",
        "%s/payg_backup_instructions" % expanduser("~"),
    )
    write_to_file(
        "/var/lib/eos-image-defaults/vendor-customer-support.ini", payg_instructions
    )


def run_key_install():
    keygen_output = generate_key()
    backup_file(
        "/usr/local/share/eos-payg/key", "%s/payg_backup_key" % expanduser("~")
    )
    add_key()
    try:
        os.remove("/var/lib/eos-payg/used-codes")
    except FileNotFoundError:
        pass
        # file doesn't exist
    generate_codes(keygen_output, read_machine_id())


def read_machine_id():
    machine_id = None
    with open("/etc/machine-id", "r") as f:
        machine_id = f.read()
        # eg '0ce890737896474589e7f3dece38cd7b'
    global device_id
    # device id currently set as first 8 characters of machine_id string
    device_id = machine_id[0:8].upper()
    print(device_id)
    return device_id


def key_gen(key, device_id):
    "generates 3 test codes to based on installed key. Period currently set to 1h"
    # implements  https://github.com/endlessm/eos-payg/blob/master/libeos-payg-codes/codes.c
    SIGN_WIDTH_BITS = 13
    COUNTER_WIDTH_BITS = 8
    period = 3
    counter_list = [250, 249, 248]
    test_code_list = []
    for count in counter_list:
        key_bytes = bytes(key, "utf-8")
        final_code = None
        digest_maker = None
        digest_maker = hmac.new(key_bytes, None, hashlib.sha1)
        digest_maker.update(struct.pack("B", period))
        digest_maker.update(struct.pack("B", count))
        hmac_data = [0]
        hmac_data = bytearray(digest_maker.digest())
        sign_mask = (1 << SIGN_WIDTH_BITS) - 1
        sign_result = ((hmac_data[18]) << 8 | (hmac_data[19])) & sign_mask
        code_value = (
            (period << (COUNTER_WIDTH_BITS + SIGN_WIDTH_BITS))
            | (count << SIGN_WIDTH_BITS)
            | sign_result
        )
        if len(str(code_value)) < 8:
            final_code = "0" + str(code_value)
        else:
            final_code = str(code_value)
        test_code_list.append(final_code)

    return {"device_id": device_id, "test_codes": test_code_list, "key": key}


def write_to_csv(test_code_data):
    """write test unlock codes to csv file"""
    fields_exist = False
    csv_file_path = cwd + key_code_file_name + ".csv"
    try:
        with open(csv_file_path, "a") as csv_file:
            if "device_id" in csv_file.read():
                fields_exist = True
            else:
                fields_exist = False

    except IOError:
        fields_exist = False
    with open(csv_file_path, "a") as csv_file:
        file_wrt = csv.writer(csv_file)
        if fields_exist:
            file_wrt.writerow(
                [
                    test_code_data["device_id"],
                    test_code_data["test_codes"][0],
                    test_code_data["test_codes"][1],
                    test_code_data["test_codes"][1],
                    test_code_data["key"],
                ]
            )
        else:
            file_wrt.writerow(["device_id", "code1", "code2", "code3", "key"])
            file_wrt.writerow(
                [
                    test_code_data["device_id"],
                    test_code_data["test_codes"][0],
                    test_code_data["test_codes"][1],
                    test_code_data["test_codes"][2],
                    test_code_data["key"],
                ]
            )


def write_to_json(test_code_data):
    current_payg_data = []
    json_file_path = cwd + key_code_file_name + ".json"
    try:
        with open(json_file_path) as f:
            current_payg_data = json.load(f)
    except IOError:
        # eg, file doesn't exist
        pass
    key_obj = {
        "device_id": test_code_data["device_id"],
        "code1": test_code_data["test_codes"][0],
        "code2": test_code_data["test_codes"][1],
        "code3": test_code_data["test_codes"][2],
        "key": test_code_data["key"],
    }
    current_payg_data.append(key_obj)
    with open(json_file_path, mode="w", encoding="utf-8") as test_codes:
        json.dump(current_payg_data, test_codes)


if __name__ == "__main__":
    set_usb_path()
    run_key_install()
    install_instructions()
