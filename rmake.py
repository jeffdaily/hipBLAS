#!/usr/bin/python3
"""Copyright (C) 2020-2022 Advanced Micro Devices, Inc. All rights reserved.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

Manage build and installation"""

import re
import sys
import os
import platform
import subprocess
import argparse
import pathlib
from fnmatch import fnmatchcase

args = {}
param = {}
OS_info = {}

def parse_args():
    """Parse command-line arguments"""
    parser = argparse.ArgumentParser(description="""Checks build arguments""")

    parser.add_argument('-b', '--rocblas', dest='rocblas_version', type=str, required=False, default="",
                        help='Specify rocblas version (e.g. 2.42.0). (optional).')

    parser.add_argument(       '--address-sanitizer', dest='address_sanitizer', required=False, default=False,
                        help='uild with address sanitizer enabled. (optional, default: False)')

    parser.add_argument(      '--build_dir', type=str, required=False, default = "build",
                        help='Configure & build process output directory.(optional, default: ./build)')

    parser.add_argument('-c', '--clients', required=False, default = False, dest='build_clients', action='store_true',
                        help='Build the library clients benchmark and gtest (optional, default: False. Generated binaries will be located at builddir/clients/staging)')

    parser.add_argument(      '--cmake-arg', dest='cmake_args', type=str, required=False, default="",
                        help='Forward the given arguments to CMake when configuring the build.')

    parser.add_argument(      '--cmake-darg', required=False, dest='cmake_dargs', action='append', default=[],
                        help='List of additional cmake defines for builds (optional, e.g. CMAKE)')

    parser.add_argument(      '--codecoverage', required=False, default=False, action='store_true',
                        help='Code coverage build. Requires Debug (-g|--debug) or RelWithDebInfo mode (-k|--relwithdebinfo), (optional, default: False)')

    parser.add_argument('-g', '--debug', required=False, default = False, action='store_true',
                        help='Build in Debug mode.(optional, default: False)')

    parser.add_argument('-i', '--install', required=False, default = False, dest='install', action='store_true',
                        help='Generate and install library package after build. (optional, default: False)')

    parser.add_argument('-k', '--relwithdebinfo', required=False, default = False, action='store_true',
                        help='Build in Release with Debug Info (optional, default: False)')

    parser.add_argument('-n', '--no-solver', dest='build_solver', required=False, default=True, action='store_false',
                        help='Build hipLBAS library without rocSOLVER dependency')

    parser.add_argument( '-r', '--relocatable', required=False, default=False, action='store_true',
                        help='Linux only: Add RUNPATH (based on ROCM_RPATH) and remove ldconf entry.')

    parser.add_argument('--rocblas-path', dest='rocblas_path', type=str, required=False, default="C:/hipSDK",
                        help='Specify path to an existing rocBLAS install directory(optional, e.g. /src/rocBLAS/build/release/rocblas-install).')

    parser.add_argument('--rocsolver-path', dest='rocsolver_path', type=str, required=False, default="C:/hipSDK",
                        help='Specify path to an existing rocSOLVER install directory (optional, e.g. /src/rocSOLVER/build/release/rocsolver-install).')

    parser.add_argument(      '--rocm_dev', type=str, required=False, default = "",
                        help='Specify specific rocm-dev version.(e.g. 4.5.0)')

    parser.add_argument(      '--skip_ld_conf_entry', required=False, default = False, help='Linux only: Skip ld.so.conf entry.')

    parser.add_argument(      '--static', required=False, default = False, dest='static_lib', action='store_true',
                        help='Build hipblas as a static library.(optional, default: False). hipblas must be built statically when the used companion rocblas is also static')

    parser.add_argument('-v', '--verbose', required=False, default = False, action='store_true',
                        help='Verbose build (optional, default: False)')

    return parser.parse_args()

def os_detect():
    global OS_info
    if os.name == "nt":
        OS_info["ID"] = platform.system()
    else:
        inf_file = "/etc/os-release"
        if os.path.exists(inf_file):
            with open(inf_file) as f:
                for line in f:
                    if "=" in line:
                        k,v = line.strip().split("=")
                        OS_info[k] = v.replace('"','')
    OS_info["NUM_PROC"] = os.cpu_count()
    print(OS_info)

def create_dir(dir_path):
    full_path = ""
    if os.path.isabs(dir_path):
        full_path = dir_path
    else:
        full_path = os.path.join( os.getcwd(), dir_path )
    pathlib.Path(full_path).mkdir(parents=True, exist_ok=True)
    return

def delete_dir(dir_path) :
    if (not os.path.exists(dir_path)):
        return
    if os.name == "nt":
        run_cmd( "RMDIR" , f"/S /Q {dir_path}")
    else:
        run_cmd( "rm" , f"-rf {dir_path}")

def cmake_path(os_path):
    if os.name == "nt":
        return os_path.replace("\\", "/")
    else:
        return os.path.realpath(os_path)

def config_cmd():
    global args
    global OS_info
    cwd_path = os.getcwd()
    cmake_executable = "cmake"
    cmake_options = []
    src_path = cmake_path(cwd_path)
    cmake_platform_opts = []
    if os.name == "nt":
        generator = f"-G Ninja"
        cmake_options.append( generator )
        # CMAKE_PREFIX_PATH set to rocm_path and HIP_PATH set BY SDK Installer
        raw_rocm_path = cmake_path(os.getenv('HIP_PATH', "C:/hip"))
        rocm_path = f'"{raw_rocm_path}"' # guard against spaces in path
        cmake_executable = "cmake"
        #set CPACK_PACKAGING_INSTALL_PREFIX= defined as blank as it is appended to end of path for archive creation
        cmake_platform_opts.append( f"-DCPACK_PACKAGING_INSTALL_PREFIX=" )
        cmake_platform_opts.append( f"-DCMAKE_INSTALL_PREFIX=\"C:/hipSDK\"" )
        toolchain = os.path.join( src_path, "toolchain-windows.cmake" )
    else:
        rocm_path = os.getenv( 'ROCM_PATH', "/opt/rocm")
        if (OS_info["ID"] in ['centos', 'rhel']):
          cmake_executable = "cmake" # was cmake3 but now we built cmake
        else:
          cmake_executable = "cmake"
        cmake_platform_opts.append( f"-DROCM_DIR:PATH={rocm_path} -DCPACK_PACKAGING_INSTALL_PREFIX={rocm_path}" )
        cmake_platform_opts.append( f'-DCMAKE_INSTALL_PREFIX="hipblas-install"' )
        toolchain = "toolchain-linux.cmake"

    print( f"Build source path: {src_path}")

    tools = f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"
    cmake_options.append( tools )

    cmake_options.extend( cmake_platform_opts )

    if args.cmake_args:
        cmake_options.append( args.cmake_args )

    cmake_base_options = f"-DROCM_PATH={rocm_path} -DCMAKE_PREFIX_PATH:PATH={rocm_path}"
    cmake_options.append( cmake_base_options )

    # packaging options
    cmake_pack_options = f"-DCPACK_SET_DESTDIR=OFF"
    cmake_options.append( cmake_pack_options )

    if os.getenv('CMAKE_CXX_COMPILER_LAUNCHER'):
        cmake_options.append( f'-DCMAKE_CXX_COMPILER_LAUNCHER={os.getenv("CMAKE_CXX_COMPILER_LAUNCHER")}' )

    # build type
    cmake_config = ""
    build_dir = os.path.realpath(args.build_dir)
    if args.debug:
        build_path = os.path.join(build_dir, "debug")
        cmake_config="Debug"
    elif args.relwithdebinfo:
        build_path = os.path.join(build_dir, "release-debug")
        cmake_config ="RelWithDebInfo"
    else:
        build_path = os.path.join(build_dir, "release")
        cmake_config="Release"

    cmake_options.append( f"-DCMAKE_BUILD_TYPE={cmake_config}" )

    if args.codecoverage:
        if args.debug or args.relwithdebinfo:
            cmake_options.append( f"-DBUILD_CODE_COVERAGE=ON" )
        else:
            os.exit( "*** Code coverage is not supported for Release build! Aborting. ***" )

    if args.address_sanitizer:
        cmake_options.append( f"-DBUILD_ADDRESS_SANITIZER=ON" )

    # clean
    delete_dir( build_path )

    create_dir( os.path.join(build_path, "clients") )
    os.chdir( build_path )

    if args.static_lib:
        cmake_options.append( f"-DBUILD_SHARED_LIBS=OFF" )

    if args.relocatable:
        rocm_rpath = os.getenv( 'ROCM_RPATH', "/opt/rocm/lib:/opt/rocm/lib64")
        cmake_options.append( f'-DCMAKE_SHARED_LINKER_FLAGS=" -Wl,--enable-new-dtags -Wl,--rpath,{rocm_rpath}"' )

    if args.skip_ld_conf_entry or args.relocatable:
        cmake_options.append( f"-DROCM_DISABLE_LDCONFIG=ON" )

    if args.build_clients:
        cmake_build_dir = cmake_path(build_dir)
        cmake_options.append( f"-DBUILD_CLIENTS_TESTS=ON -DBUILD_CLIENTS_BENCHMARKS=ON -DBUILD_CLIENTS_SAMPLES=ON -DBUILD_DIR={cmake_build_dir} " )
        # if os.name != "nt":
        #     cmake_options.append( f"-DLINK_BLIS=ON")

    if args.build_solver:
        cmake_options.append (f"-DBUILD_WITH_SOLVER=ON")
    else:
        cmake_options.append(f"-DBUILD_WITH_SOLVER=OFF")

    raw_rocblas_path = cmake_path(args.rocblas_path)
    raw_rocsolver_path = cmake_path(args.rocsolver_path)
    rocblas_path_cmake =  f'"{raw_rocblas_path}"'
    rocsolver_path_cmake =  f'"{raw_rocsolver_path}"'

    cmake_options.append( f"-DROCBLAS_PATH={rocblas_path_cmake}")
    cmake_options.append( f"-DROCSOLVER_PATH={rocsolver_path_cmake}")

    if args.cmake_dargs:
        for i in args.cmake_dargs:
          cmake_options.append( f"-D{i}" )

    cmake_options.append( f"{src_path}")
    cmd_opts = " ".join(cmake_options)

    return cmake_executable, cmd_opts


def make_cmd():
    global args
    global OS_info

    make_options = []

    nproc = OS_info["NUM_PROC"]
    if os.name == "nt":
        make_executable = f"cmake.exe --build . " # ninja
        if args.verbose:
          make_options.append( "--verbose" )
        make_options.append( "--target all" )
        if args.install:
          make_options.append( "--target package --target install" )
    else:
        make_executable = f"make -j{nproc}"
        if args.verbose:
          make_options.append( "VERBOSE=1" )
        if True: # args.install:
         make_options.append( "install" )
    cmd_opts = " ".join(make_options)

    return make_executable, cmd_opts

def run_cmd(exe, opts):
    program = f"{exe} {opts}"
    print(program)
    proc = subprocess.run(program, check=True, stderr=subprocess.STDOUT, shell=True)
    return proc.returncode

def main():
    global args
    os_detect()
    args = parse_args()

    # configure
    exe, opts = config_cmd()
    run_cmd(exe, opts)

    # make
    exe, opts = make_cmd()
    run_cmd(exe, opts)

if __name__ == '__main__':
    main()

