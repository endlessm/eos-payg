#!/bin/bash

sha256sum "$1" | sed -e s\\"$1"\\`basename "$1"`\\g
