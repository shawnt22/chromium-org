# -*- bazel-starlark -*-
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Siso configuration for ChromeOS builds."""

load("@builtin//lib/gn.star", "gn")
load("@builtin//path.star", "path")
load("@builtin//struct.star", "module")
load("./config.star", "config")

def __cros_gn_args(ctx):
    """Returns all CrOS specific toolchain and sysroot GN args."""
    if not "args.gn" in ctx.metadata:
        print("no args.gn")
        return {}
    gn_args = gn.args(ctx)
    if gn_args.get("target_os") != '"chromeos"':
        return {}

    cros_args = {}
    for arg in [
        "cros_target_ar",
        "cros_target_cc",
        "cros_target_cxx",
        "cros_target_ld",
        "cros_target_nm",
        "cros_target_readelf",
        "target_sysroot",
    ]:
        if arg not in gn_args:
            print("no " + arg)
            continue
        fp = ctx.fs.canonpath(gn_args.get(arg).strip('"'))
        cros_args[arg] = fp
        if arg == "cros_target_cxx":
            cros_args["cros_toolchain"] = path.dir(path.dir(fp))
    return cros_args

def __filegroups(ctx):
    fg = {}
    cros_args = __cros_gn_args(ctx)
    toolchain = cros_args.get("cros_toolchain")
    if toolchain:
        print("toolchain = %s" % toolchain)
        if toolchain == "/usr":
            # cros chroot ebuild.
            fg[toolchain + ":headers"] = {
                "type": "glob",
                "includes": [
                    "include/*.h",
                    "include/*/*.h",
                    "include/*/*/*.h",
                    "include/*/*/*/*.h",
                    "include/c++/*/*",
                    "bin/ccache",
                    "bin/*clang*",
                    "bin/sysroot_wrapper*",
                    "lib",
                    "lib64/*.so.*",
                    "lib64/clang/*/include/*",
                    "lib64/clang/*/include/*/*",
                    "lib64/*/include/*.h",
                ],
            }
            fg["/lib64:solibs"] = {
                "type": "glob",
                "includes": [
                    "*.so.*",
                ],
            }
        else:
            fg[toolchain + ":headers"] = {
                "type": "glob",
                # TODO: Avoid using "*" to include only required files.
                "includes": ["*"],
            }
        fg[path.join(toolchain, "bin") + ":llddeps"] = {
            "type": "glob",
            "includes": [
                "*lld*",
                "*clang*",
                "llvm-nm*",
                "llvm-readelf*",
                "llvm-readobj*",
            ],
        }
        fg[path.join(toolchain, "usr/bin") + ":llddeps"] = {
            "type": "glob",
            "includes": [
                "*lld*",
                "*clang*",
                "sysroot_wrapper*",
                "llvm-nm*",
                "llvm-readelf*",
                "llvm-readobj*",
            ],
        }
        fg[path.join(toolchain, "lib") + ":libs"] = {
            "type": "glob",
            "includes": ["*.so", "*.so.*", "*.a", "*.o"],
        }
        fg[path.join(toolchain, "lib64") + ":libs"] = {
            "type": "glob",
            "includes": ["*.so", "*.so.*", "*.a", "*.o"],
        }
        fg[path.join(toolchain, "usr/lib64") + ":libs"] = {
            "type": "glob",
            "includes": ["*.so", "*.so.*", "*.a", "*.o", "cfi_ignorelist.txt"],
        }
        fg[path.join(toolchain, "usr/armv7a-cros-linux-gnueabihf") + ":libs"] = {
            "type": "glob",
            "includes": ["*.so", "*.so.*", "*.a", "*.o"],
        }
        fg[path.join(toolchain, "usr/bin") + ":clang"] = {
            "type": "glob",
            "includes": [
                "*clang*",
                "sysroot_wrapper.hardened.ccache*",
            ],
        }

    sysroot = cros_args.get("target_sysroot")
    if sysroot:
        print("sysroot = %s" % sysroot)
        fg[path.join(sysroot, "usr/include") + ":include"] = {
            "type": "glob",
            "includes": ["*"],
            # needs bits/stab.def, c++/*
        }
        fg[path.join(sysroot, "usr/lib") + ":headers"] = {
            "type": "glob",
            "includes": ["*.h", "crtbegin.o"],
        }
        fg[path.join(sysroot, "usr/lib64") + ":headers"] = {
            "type": "glob",
            "includes": ["*.h"],
        }
        fg[sysroot + ":libs"] = {
            "type": "glob",
            "includes": ["*.so", "*.so.*", "*.a", "*.o"],
        }
    print(fg)
    return fg

def __step_config(ctx, step_config):
    cros_args = __cros_gn_args(ctx)
    toolchain = cros_args.get("cros_toolchain")
    sysroot = cros_args.get("target_sysroot")
    if not (toolchain and sysroot):
        return step_config

    cros_target_cxx = cros_args.get("cros_target_cxx")
    if cros_target_cxx:
        if path.isabs(cros_target_cxx):
            # cros chroot ebuild
            step_config["rules"].extend([
                {
                    "name": "clang-cros/cxx",
                    "action": "(.*_)?cxx",
                    "remote": True,
                    "inputs": [
                        cros_target_cxx,
                    ],
                    # fast-deps is not safe with cros toolchain. crbug.com/391160876
                    "no_fast_deps": True,
                    "timeout": "5m",
                },
            ])
            step_config["input_deps"].update({
                cros_target_cxx: [
                    "/lib",
                    "/lib64:solibs",
                ],
            })
        else:
            step_config["rules"].extend([
                {
                    "name": "clang-cros/cxx",
                    "action": "(.*_)?cxx",
                    "command_prefix": path.join("../../", cros_target_cxx),
                    "remote": True,
                    # fast-deps is not safe with cros toolchain. crbug.com/391160876
                    "no_fast_deps": True,
                    "canonicalize_dir": True,
                    "timeout": "5m",
                },
            ])

    cros_target_cc = cros_args.get("cros_target_cc")
    if cros_target_cc:
        if path.isabs(cros_target_cc):
            # cros chroot ebuild
            step_config["rules"].extend([
                {
                    "name": "clang-cros/cc",
                    "action": "(.*_)?cc",
                    "remote": True,
                    "inputs": [
                        cros_target_cc,
                    ],
                    # fast-deps is not safe with cros toolchain. crbug.com/391160876
                    "no_fast_deps": True,
                    "timeout": "5m",
                },
            ])
            step_config["input_deps"].update({
                cros_target_cc: [
                    "/lib",
                    "/lib64:solibs",
                ],
            })
        else:
            step_config["rules"].extend([
                {
                    "name": "clang-cros/cc",
                    "action": "(.*_)?cc",
                    "command_prefix": path.join("../../", cros_target_cc),
                    "remote": True,
                    # fast-deps is not safe with cros toolchain. crbug.com/391160876
                    "no_fast_deps": True,
                    "canonicalize_dir": True,
                    "timeout": "5m",
                },
            ])

    cros_target_ar = cros_args.get("cros_target_ar")
    if cros_target_ar:
        step_config["rules"].extend([
            {
                "name": "clang-cros/alink/llvm-ar",
                # Other alink steps should use clang/alink/llvm-ar rule.
                "action": "(target_with_system_allocator_)?alink",
                "inputs": [
                    cros_target_ar,
                ],
                "exclude_input_patterns": [
                    "*.cc",
                    "*.h",
                    "*.js",
                    "*.pak",
                    "*.py",
                ],
                "handler": "lld_thin_archive",
                "remote": config.get(ctx, "remote-link"),
                "canonicalize_dir": True,
                "timeout": "5m",
                "platform_ref": "large",
                "accumulate": True,
            },
        ])
        step_config["input_deps"].update({
            cros_target_ar: [
                path.join(toolchain, "bin/llvm-ar.elf"),
                path.join(toolchain, "lib") + ":libs",
                path.join(toolchain, "usr/lib64") + ":libs",
            ],
        })

    step_config["rules"].extend([
        {
            "name": "clang-cros/solink/gcc_solink_wrapper",
            "action": "(target_with_system_allocator_)?solink",
            "command_prefix": "\"python3\" \"../../build/toolchain/gcc_solink_wrapper.py\"",
            "inputs": [
                "build/toolchain/gcc_solink_wrapper.py",
                path.join(toolchain, "bin/ld.lld"),
            ],
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
            ],
            "remote": config.get(ctx, "remote-link"),
            # TODO: Do not use absolute paths for custom toolchain/sysroot GN
            # args.
            "input_root_absolute_path": True,
            "platform_ref": "large",
            "timeout": "2m",
        },
        {
            "name": "clang-cros/link/gcc_link_wrapper",
            "action": "(target_with_system_allocator_)?link",
            "command_prefix": "\"python3\" \"../../build/toolchain/gcc_link_wrapper.py\"",
            "handler": "clang_link",
            "inputs": [
                "build/toolchain/gcc_link_wrapper.py",
                path.join(toolchain, "bin/ld.lld"),
            ],
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
            ],
            "remote": config.get(ctx, "remote-link"),
            "canonicalize_dir": True,
            "platform_ref": "large",
            "timeout": "10m",
        },
    ])
    step_config["input_deps"].update({
        sysroot + ":headers": [
            path.join(sysroot, "usr/include") + ":include",
            path.join(sysroot, "usr/lib") + ":headers",
            path.join(sysroot, "usr/lib64") + ":headers",
        ],
        path.join(toolchain, "bin/llvm-ar"): [
            path.join(toolchain, "bin/llvm-ar.elf"),
            path.join(toolchain, "lib") + ":libs",
            path.join(toolchain, "usr/lib64") + ":libs",
        ],
        path.join(toolchain, "bin/ld.lld"): [
            path.join(toolchain, "bin:llddeps"),
            path.join(toolchain, "lib") + ":libs",
            path.join(toolchain, "lib64") + ":libs",
            path.join(toolchain, "usr/bin:clang"),
            path.join(toolchain, "usr/lib64") + ":libs",
            sysroot + ":libs",
        ],
        toolchain + ":link": [
            path.join(toolchain, "bin") + ":llddeps",
            path.join(toolchain, "lib") + ":libs",
            path.join(toolchain, "lib64") + ":libs",
            path.join(toolchain, "usr/bin") + ":llddeps",
            path.join(toolchain, "usr/lib64") + ":libs",
        ],
        sysroot + ":link": [
            sysroot + ":libs",
        ],
    })
    return step_config

cros = module(
    "cros",
    filegroups = __filegroups,
    handlers = {},
    step_config = __step_config,
)
