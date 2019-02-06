#!/usr/bin/env python
# -*- coding: utf-8 -*-

import argparse
import contextlib
import glob
import json
import os
import shutil
import subprocess
import tempfile

_DCH_VENDOR_FLAG = None

@contextlib.contextmanager
def inside_temporary_directory(dir=None):
    curdir = os.getcwd()
    directory = tempfile.mkdtemp(dir=dir)
    os.chdir(directory)
    try:
        yield directory
    finally:
        shutil.rmtree(directory)
        os.chdir(curdir)

@contextlib.contextmanager
def cwd(dir):
    prevdir = os.getcwd()
    os.chdir(dir)
    try:
        yield
    finally:
        os.chdir(prevdir)

@contextlib.contextmanager
def symlinked(src, dstdir):
    dst = os.path.join(dstdir, os.path.basename(src))
    if os.path.lexists(dst):
        os.remove(dst)
    os.symlink(src, dst)
    try:
        yield
    finally:
        os.remove(dst)

def reset_debian_changelog():
    if not os.path.exists("debian"):
        os.mkdir("debian")
    if os.path.lexists("debian/changelog"):
        os.remove("debian/changelog")

def dch(version, message, create_package=None):
    global _DCH_VENDOR_FLAG
    if _DCH_VENDOR_FLAG is None:
        dch_help = subprocess.check_output(["dch", "--help"])
        # Check sanity: we expect that either --vendor or --distributor is supported (but not both).
        if "--vendor" in dch_help != "--distributor" not in dch_help:
            raise RuntimeError("Cannot decide what flag to use with dch --vendor or --distributor")
        _DCH_VENDOR_FLAG = "--vendor" if "--vendor" in dch_help else "--distributor"

    args = ["dch"]
    if create_package is not None:
        args += ["--create", "--package", create_package]
    args += [
        _DCH_VENDOR_FLAG, "yandex",
        "--distribution", "unstable",
        "--urgency", "low",
        "--force-distribution",
        "--newversion", version,
        message
    ]
    subprocess.check_call(args)

def main(args):
    args.install_dir = os.path.realpath(args.install_dir)
    config_generator = os.path.join(args.install_dir, "build_python_packages_config_generator")
    driver_lib_file = os.path.join(args.install_dir, "driver_lib.so")
    yson_lib_file = os.path.join(args.install_dir, "yson_lib.so")
    for fname in [driver_lib_file, yson_lib_file]:
        if not os.path.exists(fname):
            raise RuntimeError("File {basename} cannot be found inside install dir: {installdir}".format(
                basename=os.path.basename(fname),
                installdir=args.install_dir
            ))

    config = json.loads(subprocess.check_output([config_generator]))
    yt_version = config["yt_version"]
    yt_rpc_proxy_protocol_version = config["yt_rpc_proxy_protocol_version"]
    source_directory = os.path.realpath(args.source_dir)
    output_directory = os.path.realpath(args.output_dir)
    work_directory = os.path.realpath(args.work_dir)

    if not os.path.exists(output_directory):
        os.mkdir(output_directory)

    with inside_temporary_directory(dir=work_directory) as tmpdir:
        python_src_copy = os.path.realpath("python_src_copy")

        shutil.copytree(
            os.path.join(source_directory, "python"),
            python_src_copy,
            symlinks=True)

        # Create changelog for yandex-yt-python-driver
        with inside_temporary_directory(dir=work_directory):
            os.mkdir("debian")
            shutil.copy(os.path.join(source_directory, "debian/changelog"), "debian/changelog")
            dch(version=yt_version, message="Package version bump; no source changes.")
            with open("debian/changelog") as inf:
                text = inf.read()
            with open(os.path.join(python_src_copy, "yandex-yt-python-driver/debian/changelog"), "w") as outf:
                outf.write(text.replace("yandex-yt", "yandex-yt-python-driver"))

        with cwd(os.path.join(python_src_copy, "yandex-yt-python-proto")):
            reset_debian_changelog()
            dch(version=yt_rpc_proxy_protocol_version,
                message="Proto package release.",
                create_package="yandex-yt-python-proto")

        with symlinked(driver_lib_file, os.path.join(python_src_copy, "yt_driver_bindings")), \
                symlinked(yson_lib_file, os.path.join(python_src_copy, "yt_yson_bindings")):
            subprocess.check_call(["./build_proto.sh"], cwd=python_src_copy)

        package_list = glob.glob("yandex-yt-python*")
        if not package_list:
            raise AssertionError("Script didn't generated any packages, looks like a bug")

        for file in package_list:
            shutil.move(file, os.path.join(output_directory, file))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-dir",
        required=True,
        help="path to source directory")

    parser.add_argument(
        "--install-dir",
        required=True,
        help="path to install dir used during ya build (we expect to find config file and .so libraries there)")

    parser.add_argument(
        "--output-dir",
        required=True,
        help="where to put resulting deb files")

    parser.add_argument(
        "--work-dir",
        help="where to store temporary files")

    args = parser.parse_args()
    main(args)
