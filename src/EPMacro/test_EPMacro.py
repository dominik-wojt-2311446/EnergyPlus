#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# EnergyPlus, Copyright (c) 1996-2025, The Board of Trustees of the University
# of Illinois, The Regents of the University of California, through Lawrence
# Berkeley National Laboratory (subject to receipt of any required approvals
# from the U.S. Dept. of Energy), Oak Ridge National Laboratory, managed by UT-
# Battelle, Alliance for Sustainable Energy, LLC, and other contributors. All
# rights reserved.
#
# NOTICE: This Software was developed under funding from the U.S. Department of
# Energy and the U.S. Government consequently retains certain rights. As such,
# the U.S. Government has been granted for itself and others acting on its
# behalf a paid-up, nonexclusive, irrevocable, worldwide license in the
# Software to reproduce, distribute copies to the public, prepare derivative
# works, and perform publicly and display publicly, and to permit others to do
# so.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# (1) Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#
# (2) Redistributions in binary form must reproduce the above copyright notice,
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
#
# (3) Neither the name of the University of California, Lawrence Berkeley
#     National Laboratory, the University of Illinois, U.S. Dept. of Energy nor
#     the names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
# (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in
#     stand-alone form without changes from the version obtained under this
#     License, or (ii) Licensee makes a reference solely to the software
#     portion of its product, Licensee must refer to the software as
#     "EnergyPlus version X" software, where "X" is the version number Licensee
#     obtained under this License and may not use a different name for the
#     software. Except as specifically required in this Section (4), Licensee
#     shall not use in a company name, a product name, in advertising,
#     publicity, or other promotional activities any name, trade name,
#     trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or
#     confusingly similar designation, without the U.S. Department of Energy's
#     prior written consent.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""
Test EPMacro.

This script aims to check that the EPMacro exe correctly handles macros in the
IDF.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import filecmp

INPUT_FILE_NAME = "in.imf"
OUTPUT_FILE_NAME = "out.idf"
AUDIT_FILE_NAME = "audit.out"


def parse_args():
    """Set up argument parser."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--epmacro-exe",
        required=True,
        type=Path,
        help="Path to the EPMacro exe",
    )
    parser.add_argument(
        "--input-dir",
        required=True,
        type=Path,
        help="Directory containing input and reference output",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        type=Path,
        help="Output directory",
    )

    args = parser.parse_args()
    return args


def clean_up_directory(output_dir: Path):
    """Delete and remake directory."""
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)


def run_epmacro(epmacro_exe: Path, input_dir: Path, output_dir: Path):
    """Run the program and compare output."""
    if not epmacro_exe.exists():
        raise ValueError(f"EPMacro exe '{epmacro_exe}' does not exist")

    if not output_dir.exists() or not output_dir.is_dir():
        raise ValueError(f"out_dir '{output_dir}' is not a valid directory")

    if not input_dir.exists() or not input_dir.is_dir():
        raise ValueError(f"input_dir '{input_dir}' is not a valid directory")

    reference_input_path = input_dir.joinpath(INPUT_FILE_NAME)
    reference_output_path = input_dir.joinpath(OUTPUT_FILE_NAME)
    reference_audit_path = input_dir.joinpath(AUDIT_FILE_NAME)

    if not reference_input_path.exists() or not reference_input_path.is_file():
        raise ValueError(
            f"reference_input_path '{reference_input_path}' is not a valid file"
        )

    if not reference_output_path.exists() or not reference_output_path.is_file():
        raise ValueError(
            f"reference_output_path '{reference_output_path}' is not a valid file"
        )

    if not reference_audit_path.exists() or not reference_audit_path.is_file():
        raise ValueError(
            f"reference_input_path '{reference_audit_path}' is not a valid file"
        )

    temporary_input_path = output_dir.joinpath(INPUT_FILE_NAME)
    temporary_output_path = output_dir.joinpath(OUTPUT_FILE_NAME)
    temporary_audit_path = output_dir.joinpath(AUDIT_FILE_NAME)

    shutil.copy(reference_input_path, temporary_input_path)

    full_command_args = [epmacro_exe]
    command_as_strings = [str(x) for x in full_command_args]
    print(f"Running: {' '.join(command_as_strings)}")
    subprocess.check_call(command_as_strings, cwd=output_dir)

    if not temporary_output_path.exists() or not temporary_output_path.is_file():
        raise ValueError(
            f"Test Failed: output file '{temporary_output_path}' does not exist!"
        )

    if not temporary_audit_path.exists() or not temporary_audit_path.is_file():
        raise ValueError(
            f"Test Failed: output file '{temporary_audit_path}' does not exist!"
        )

    if not filecmp.cmp(reference_output_path, temporary_output_path):
        raise ValueError(
            f"Test Failed: output file '{temporary_output_path}' "
            f"is different to the reference file '{reference_output_path}'!"
        )

    # Content of the audit files are not specified and the new implementation of
    # EPMacro does not try to reproduce the old output
    # if not filecmp.cmp(reference_audit_path, temporary_audit_path):
    #     raise ValueError(
    #         f"Test Failed: output file '{temporary_audit_path}' "
    #         f"is different to the reference file '{reference_audit_path}'!"
    #     )


if __name__ == "__main__":

    args = parse_args()
    print(args)

    clean_up_directory(output_dir=args.output_dir)
    run_epmacro(
        epmacro_exe=args.epmacro_exe,
        input_dir=args.input_dir,
        output_dir=args.output_dir,
    )
