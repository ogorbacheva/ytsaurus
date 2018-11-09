#!/usr/bin/env python
# -*- coding: utf-8 -*-

import argparse
import glob
import os
import shutil

PYTHON_PACKAGE_LIST = [
    "argcomplete",
    "backports_abc",
    "certifi",
    "dill",
    "requests",
    "singledispatch",
    "six",
    "tornado",
    "distro",
]


def rm_rf(path):
    "remove recursive"
    if os.path.isdir(path):
        shutil.rmtree(path)
    else:
        os.unlink(path)


def cp_r(path, dest_dir):
    "copy recursive"
    assert os.path.isdir(dest_dir)
    if os.path.isdir(path):
        shutil.copytree(path, os.path.join(dest_dir, os.path.basename(path)))
    else:
        shutil.copy2(path, dest_dir)


def replace(path, dest_dir):
    dst_path = os.path.join(dest_dir, os.path.basename(path))
    if os.path.exists(dst_path):
        rm_rf(dst_path)
    cp_r(path, dest_dir)


def replace_symlink(source, destination):
    if os.path.lexists(destination):
        os.remove(destination)
    os.symlink(source, destination)


def prepare_python_source_tree(python_root):
    def python_contrib_path(path):
        return os.path.join(python_root, "contrib", path)

    packages_dir = os.path.join(python_root, "yt", "packages")

    # Cleanup old stuff
    for fname in os.listdir(packages_dir):
        for package in PYTHON_PACKAGE_LIST:
            if fname.startswith(package):
                rm_rf(os.path.join(packages_dir, fname))

    for package_name in PYTHON_PACKAGE_LIST:
        files_to_copy = glob.glob(
            "{python_root}/contrib/python-{package_name}/{package_name}*.py".format(
                python_root=python_root,
                package_name=package_name))
        files_to_copy += glob.glob(
            "{python_root}/contrib/python-{package_name}/{package_name}".format(
                python_root=python_root,
                package_name=package_name))
        for path in files_to_copy:
            cp_r(path, packages_dir)

    replace(os.path.join(python_root, "yson-debian/yt_yson_bindings"), python_root)
    replace(python_contrib_path("python-decorator/src/decorator.py"), packages_dir)
    replace(python_contrib_path("python-fusepy/fuse.py"), packages_dir)


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--python-root", default=os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--fake-dependency")

    args = parser.parse_args()

    prepare_python_source_tree(python_root=args.python_root)


if __name__ == "__main__":
    main()
