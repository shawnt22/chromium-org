#!/usr/bin/env python3

# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile

# How to patch libxslt in Chromium:
#
# 1. Write a .patch file and add it to third_party/libxslt/chromium.
# 2. Apply the patch in src: patch -p1 <../chromium/foo.patch
# 3. Add the patch to the list of patches in this file.
# 4. Update README.chromium with the provenance of the patch.
# 5. Upload a change with the modified documentation, roll script,
#    patch, applied patch and any other relevant changes like
#    regression tests. Go through the usual review and commit process.
#
# How to roll libxslt in Chromium:
#
# Prerequisites:
#
# 1. Check out Chromium somewhere on Linux, Mac and Windows.
# 2. On Linux:
#    a. sudo apt-get install libicu-dev
#    b. git clone https://gitlab.gnome.org/GNOME/libxslt.git somewhere
# 3. On Mac, install these packages with brew:
#      autoconf automake libtool pkgconfig icu4c
#
# Procedure:
#
# Warning: This process is destructive. Run it on a clean branch.
#
# 1. On Linux, in the libxslt repo directory:
#    a. git remote update origin
#    b. git checkout origin/master
#
#    This will be the upstream version of libxslt you are rolling to.
#
# 2. On Linux, in the Chromium src director:
#    a. third_party/libxslt/chromium/roll.py --linux /path/to/libxslt
#
#    If this fails, it may be a patch no longer applies. Reset to
#    head; modify the patch files, this script, and
#    README.chromium; then commit the result and run it again.
#
#    b. Upload a Cl but do not start review
#
# 2. On Windows, in the Chromium src directory:
#    a. git cl patch <Gerrit Issue ID>
#    b. third_party\libxslt\chromium\roll.py --win32
#    c. git cl upload
#
# 3. On Mac, in the Chromium src directory:
#    a. git cl patch <Gerrit Issue ID>
#    b. third_party/libxslt/chromium/roll.py --mac
#    c. Make and commit any final changes to README.chromium, BUILD.gn, etc.
#    d. Complete the code review process as usual: git cl upload -d;
#       git cl try-results; etc.
#
# The --linuxfast argument is an alternative to --linux which also deletes
# files which are not intended to be checked in. This would normally happen at
# the end of the --mac run, but if you want to run the roll script and get to
# the final state without running the configure scripts on all three platforms,
# this is helpful.

PATCHES = [
    'xslt-locale.patch',
    'new-unified-atype-extra.patch',
    'fix-tracking-for-generated-IDs-for-most-XML-nodes.patch',
    '0004-Use-a-dedicated-node-type-to-maintain-the-list-of-ca.patch',
]


# See libxslt configure.ac and win32/configure.js to learn what
# options are available.

# These two sets of options should be in sync. You can check the
# generated #defines in (win32|mac|linux)/config.h to confirm
# this.
SHARED_XSLT_CONFIGURE_OPTIONS = [
    # These options are turned OFF
    ('--without-debug', 'xslt_debug=no'),
    ('--without-debugger', 'debugger=no'),
    ('--without-mem-debug', 'mem_debug=no'),
    ('--without-plugins', 'modules=no'),
    ('--without-crypto', 'crypto=no'),
    ('--without-python', 'python=no'),
]

# These options are only available in configure.ac for Linux and Mac.
EXTRA_NIX_XSLT_CONFIGURE_OPTIONS = [
]


# These options are only available in win32/configure.js for Windows.
EXTRA_WIN32_XSLT_CONFIGURE_OPTIONS = [
    'compiler=msvc',
    'iconv=no',
]


XSLT_CONFIGURE_OPTIONS = (
    [option[0] for option in SHARED_XSLT_CONFIGURE_OPTIONS] +
    EXTRA_NIX_XSLT_CONFIGURE_OPTIONS)


XSLT_WIN32_CONFIGURE_OPTIONS = (
    [option[1] for option in SHARED_XSLT_CONFIGURE_OPTIONS] +
    EXTRA_WIN32_XSLT_CONFIGURE_OPTIONS)


FILES_TO_REMOVE = [
    # TODO: Excluding ChangeLog and NEWS because encoding problems mean
    # bots can't patch these. Reinclude them when there is a consistent
    # encoding.
    'src/NEWS',
    'src/ChangeLog',
    # These are auto-generated by autoconf/automake and should not be included
    # with the source code
    'src/Makefile.in',
    'src/aclocal.m4',
    'src/CMakeLists.txt',
    'src/compile',
    'src/config.guess',
    'src/config.sub',
    'src/configure',
    'src/configure.ac',
    'src/depcomp',
    'src/install-sh',
    'src/libexslt/Makefile.in',
    'src/libxslt.spec.in',
    'src/libxslt/Makefile.in',
    'src/libxslt/libxslt.syms',
    'src/ltmain.sh',
    'src/m4/ax_append_flag.m4',
    'src/missing',
    'src/win32/Makefile.msvc',
    'src/xslt-config.in',
    # These are not needed.
    'src/doc',
    'src/python',
    'src/tests',
    'src/xsltproc',
    'src/examples',
    'src/vms',
]


THIRD_PARTY_LIBXML_LINUX = 'third_party/libxml/linux'
THIRD_PARTY_LIBXSLT = 'third_party/libxslt'
THIRD_PARTY_LIBXSLT_SRC = os.path.join(THIRD_PARTY_LIBXSLT, 'src')


def libxml_path_option(src_path):
    """Gets the path to libxml/linux in Chromium.

    libxslt needs to be configured with libxml source.

    Args:
        src_path: The Chromium src path.

    Returns:
        The path to the libxml2 third_party/libxml/linux configure
        output.
    """
    libxml_linux_path = os.path.join(src_path, THIRD_PARTY_LIBXML_LINUX)
    return ['--with-libxml-src=%s' % libxml_linux_path]


class WorkingDir(object):
    """Changes the working directory and resets it on exit."""
    def __init__(self, path):
        self.prev_path = os.getcwd()
        self.path = path

    def __enter__(self):
        os.chdir(self.path)

    def __exit__(self, exc_type, exc_value, traceback):
        if exc_value:
            print('was in %s; %s before that' % (self.path, self.prev_path))
        os.chdir(self.prev_path)


def git(*args):
    """Runs a git subcommand.

    On Windows this uses the shell because there's a git wrapper
    batch file in depot_tools.

    Arguments:
        args: The arguments to pass to git.
    """
    command = ['git'] + list(args)
    subprocess.check_call(command, shell=(os.name == 'nt'))


def remove_tracked_and_local_dir(path):
    """Removes the contents of a directory from git, and the filesystem.

    Arguments:
        path: The path to remove.
    """
    remove_tracked_files([path])
    shutil.rmtree(path, ignore_errors=True)
    os.mkdir(path)


def remove_tracked_files(files_to_remove):
    """Removes tracked files from git.

    Arguments:
        files_to_remove: The files to remove.
    """
    files_to_remove = [f for f in files_to_remove if os.path.exists(f)]
    git('rm', '-rf', '--ignore-unmatch', *files_to_remove)


def sed_in_place(input_filename, program):
    """Replaces text in a file.

    Arguments:
        input_filename: The file to edit.
        program: The sed program to perform edits on the file.
    """
    # OS X's sed requires -e
    subprocess.check_call(['sed', '-i', '-e', program, input_filename])


def check_copying(path='.'):
    path = os.path.join(path, 'COPYING')
    if not os.path.exists(path):
        return
    with open(path) as f:
        s = f.read()
        if 'GNU' in s:
            raise Exception('check COPYING')


def patch_config():
    """Changes autoconf results which can not be changed with options."""
    sed_in_place('config.h', 's/#define HAVE_CLOCK_GETTIME 1//')

    # https://crbug.com/670720
    sed_in_place('config.h', 's/#define HAVE_ASCTIME 1//')
    sed_in_place('config.h', 's/#define HAVE_LOCALTIME 1//')
    sed_in_place('config.h', 's/#define HAVE_MKTIME 1//')

    sed_in_place('config.log',
                 r's/[a-z.0-9]\+\.corp\.google\.com/REDACTED/')


def prepare_libxslt_distribution(src_path, libxslt_repo_path, temp_dir):
    """Makes a libxslt distribution.

    Args:
        src_path: The Chromium repository src path, for finding libxslt.
        libxslt_repo_path: The path to the local clone of the libxslt repo.
        temp_dir: A temporary directory to stage the distribution to.

    Returns: A tuple of commit hash and full path to the archive.
    """
    # If it was necessary to push from a distribution prepared upstream,
    # this is the point to inject it: Return the version string and the
    # distribution tar file.

    # The libxslt repo we're pulling changes from should not have
    # local changes. This *should* be a commit that's publicly visible
    # in the upstream repo; reviewers should check this.
    check_clean(libxslt_repo_path)

    temp_config_path = os.path.join(temp_dir, 'config')
    os.mkdir(temp_config_path)
    temp_src_path = os.path.join(temp_dir, 'src')
    os.mkdir(temp_src_path)

    with WorkingDir(libxslt_repo_path):
        commit = subprocess.check_output(
            ['git', 'log', '-n', '1', '--pretty=format:%H', 'HEAD']).decode('ascii')
        subprocess.check_call(
            'git archive HEAD | tar -x -C "%s"' % temp_src_path,
            shell=True)
    with WorkingDir(temp_src_path):
        os.remove('.gitignore')
        for patch in PATCHES:
            print('applying %s' % patch)
            subprocess.check_call(
                'patch -p1 --fuzz=0 < %s' % os.path.join(
                    src_path, THIRD_PARTY_LIBXSLT_SRC, '..', 'chromium', patch),
                shell=True)
    with WorkingDir(temp_config_path):
        subprocess.check_call(['../src/autogen.sh'] + XSLT_CONFIGURE_OPTIONS +
                              libxml_path_option(src_path))
        subprocess.check_call(['make', 'dist-all'])

        # Work out what it is called
        tar_file = subprocess.check_output(
            '''awk '/PACKAGE =/ {p=$3} /VERSION =/ {v=$3} '''
            '''END {printf("%s-%s.tar.xz", p, v)}' Makefile''',
            shell=True).decode('ascii')
        return commit, os.path.abspath(tar_file)


def roll_libxslt_linux(src_path, repo_path, fast):
    check_clean(src_path)
    with WorkingDir(src_path):
        try:
            temp_dir = tempfile.mkdtemp()
            print('temporary directory is: %s' % temp_dir)
            commit, tar_file = prepare_libxslt_distribution(
                src_path, repo_path, temp_dir)

            # Remove all of the old libxslt to ensure only desired
            # cruft accumulates
            remove_tracked_and_local_dir(THIRD_PARTY_LIBXSLT_SRC)

            # Export the libxslt distribution to the Chromium tree
            with WorkingDir(THIRD_PARTY_LIBXSLT_SRC):
                subprocess.check_call(
                    'tar xJf %s --strip-components=1' % tar_file,
                    shell=True)
        finally:
            shutil.rmtree(temp_dir)

        with WorkingDir(THIRD_PARTY_LIBXSLT_SRC):
            # Write the commit ID into the README.chromium file
            sed_in_place('../README.chromium',
                         's/Revision: .*$/Revision: %s/' % commit)
            # TODO(crbug.com/349529871): Use the version number instead of
            # commit hash once it has been added upstream:
            # https://gitlab.gnome.org/GNOME/libxslt/-/issues/117
            sed_in_place('../README.chromium',
                         's/Version: .*$/Version: %s/' % commit)
            check_copying()

            with WorkingDir('../linux'):
                subprocess.check_call(['../src/configure'] +
                                      XSLT_CONFIGURE_OPTIONS +
                                      libxml_path_option(src_path))
                check_copying()
                patch_config()
                # Other platforms share this, even though it is
                # generated on Linux. Android and Windows do not have
                # xlocale.
                sed_in_place('libxslt/xsltconfig.h',
                             '/Locale support/,/#if 1/s/#if 1/#if 0/')
                shutil.move('libxslt/xsltconfig.h', '../src/libxslt')

            git('add', '*')
            if fast:
                with WorkingDir('..'):
                    remove_tracked_files(FILES_TO_REMOVE)
        git('commit', '-am', '%s libxslt, linux' % commit)

        if fast:
            print('Now upload for review, etc.')
        else:
            print('Now push to Windows and runs steps there.')


def roll_libxslt_win32(src_path):
    full_path_to_libxslt = os.path.join(src_path, THIRD_PARTY_LIBXSLT)
    with WorkingDir(full_path_to_libxslt):
        with WorkingDir('src/win32'):
            # Run the configure script.
            subprocess.check_call(['cscript', '//E:jscript', 'configure.js'] +
                                  XSLT_WIN32_CONFIGURE_OPTIONS)
        shutil.copy('src/config.h', 'win32/config.h')
        git('add', 'win32/config.h')
        git('commit', '--allow-empty', '-m', 'Windows')
    print('Now push to Mac and run steps there.')


def roll_libxslt_mac(src_path):
    full_path_to_libxslt = os.path.join(src_path, THIRD_PARTY_LIBXSLT)
    with WorkingDir(full_path_to_libxslt):
        with WorkingDir('mac'):
            subprocess.check_call(['autoreconf', '-i', '../src'])
            os.chmod('../src/configure',
                     os.stat('../src/configure').st_mode | stat.S_IXUSR)
            # /linux in the configure options is not a typo; configure
            # looks here to find xml2-config
            subprocess.check_call(['../src/configure'] +
                                  XSLT_CONFIGURE_OPTIONS)
            check_copying()
            patch_config()
            # Commit and upload the result
            git('add', 'config.h')
        remove_tracked_files(FILES_TO_REMOVE)
        git('commit', '-m', 'Mac')
        print('Now upload for review, etc.')


def check_clean(path):
    with WorkingDir(path):
        status = subprocess.check_output(['git', 'status', '-s', '-uno']).decode('ascii')
        if len(status) > 0:
            raise Exception('repository at %s is not clean' % path)


def main():
    src_dir = os.getcwd()
    if not os.path.exists(os.path.join(src_dir, 'third_party')):
        print('error: run this script from the Chromium src directory')
        sys.exit(1)

    parser = argparse.ArgumentParser(
        description='Roll the libxslt dependency in Chromium')
    platform = parser.add_mutually_exclusive_group(required=True)
    platform.add_argument('--linux', action='store_true')
    platform.add_argument('--win32', action='store_true')
    platform.add_argument('--mac', action='store_true')
    platform.add_argument('--linuxfast', action='store_true')
    parser.add_argument(
        'libxslt_repo_path',
        type=str,
        nargs='?',
        help='The path to the local clone of the libxslt git repo.')
    args = parser.parse_args()

    if args.linux or args.linuxfast:
        libxslt_repo_path = args.libxslt_repo_path
        if not libxslt_repo_path:
            print('Specify the path to the local libxslt repo clone.')
            sys.exit(1)
        libxslt_repo_path = os.path.abspath(libxslt_repo_path)
        roll_libxslt_linux(src_dir, libxslt_repo_path, args.linuxfast)
    elif args.win32:
        roll_libxslt_win32(src_dir)
    elif args.mac:
        roll_libxslt_mac(src_dir)


if __name__ == '__main__':
    main()
