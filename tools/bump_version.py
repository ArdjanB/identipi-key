#!/usr/bin/env python3
"""Bump the firmware patch version and stamp the build time in version.h.

Run automatically by CMake before every build (see the bump_version
target in CMakeLists.txt). Safe to run by hand too, but there's normally
no reason to.
"""
import datetime
import re
import sys


def main():
    if len(sys.argv) != 2:
        print("usage: bump_version.py <path to version.h>", file=sys.stderr)
        return 1
    path = sys.argv[1]

    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"(\d+)\.(\d+)\.(\d+)"', text)
    if not m:
        print("FIRMWARE_VERSION not found in " + path, file=sys.stderr)
        return 1

    major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
    patch += 1
    new_version = "{}.{}.{}".format(major, minor, patch)
    text = text[:m.start()] + '#define FIRMWARE_VERSION "{}"'.format(new_version) + text[m.end():]

    build_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    build_time_re = r'#define\s+FIRMWARE_BUILD_TIME\s+"[^"]*"'
    if re.search(build_time_re, text):
        text = re.sub(build_time_re,
                      '#define FIRMWARE_BUILD_TIME "{}"'.format(build_time), text)
    else:
        text = re.sub(r'(#define\s+FIRMWARE_DATE\s+"[^"]*"\n)',
                      r'\1#define FIRMWARE_BUILD_TIME "' + build_time + '"\n',
                      text, count=1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(text)

    print("version.h: bumped to {}, build time {}".format(new_version, build_time))
    return 0


if __name__ == "__main__":
    sys.exit(main())
