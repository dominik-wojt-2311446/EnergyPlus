// EnergyPlus, Copyright (c) 1996-2025, The Board of Trustees of the University of Illinois,
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy), Oak Ridge
// National Laboratory, managed by UT-Battelle, Alliance for Sustainable Energy, LLC, and other
// contributors. All rights reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "fmt/core.h"
#include <cstddef>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

constexpr std::string_view input_file_name = "in.imf";
constexpr std::string_view output_file_name = "out.idf";
constexpr std::string_view audit_file_name = "audit.out";
constexpr std::string_view white_space = " \t";

constexpr std::string_view directive_marker{"##"};
constexpr std::string_view include_directive = "include";
constexpr std::string_view includesilent_directive = "includesilent";
constexpr std::string_view fileprefix_directive = "fileprefix";
constexpr std::string_view nosilent_directive = "nosilent";

struct macro_definition
{
    std::vector<std::string> arguments;
    std::vector<std::string> tokens;
};

struct file_pointer
{
    std::filesystem::path file;
    std::size_t line_number;
};

struct state
{
    std::vector<file_pointer> file_stack;
    std::filesystem::path prefix;
    std::map<std::string, macro_definition> macros;
};

bool starts_with(const std::string_view a, const std::string_view b)
{
    std::string_view a_start{a.begin(), std::min(a.length(), b.length())};
    return a_start == b;
}

std::string_view strip_ws(const std::string_view text)
{
    const auto begin = text.find_first_not_of(white_space);
    if (begin == std::string_view::npos) return {};
    const auto end = text.find_last_not_of(white_space) + 1;
    return {text.begin() + begin, end - begin};
}

std::optional<std::tuple<std::string_view, std::string_view>> parse_directive(const std::string_view line)
{
    std::string_view remaining{line};
    if (!starts_with(remaining, directive_marker)) {
        return {};
    }

    remaining = remaining.substr(directive_marker.length());
    const std::size_t directive_end = remaining.find_first_of(white_space);
    const std::string_view directive{remaining.substr(0, directive_end)};
    if (directive.empty()) throw std::runtime_error("Empty directive!");

    remaining = remaining.substr(directive.length());
    const std::size_t next_begin = remaining.find_first_not_of(white_space);
    const std::string_view past_directive{next_begin == std::string_view::npos ? std::string_view{} : remaining.substr(next_begin)};
    return {{directive, past_directive}};
}

void process_file(const std::filesystem::path &path, std::ofstream &out, std::ofstream &audit, state &state_0)
{
    state_0.file_stack.push_back({path, 1});

    std::ifstream in{path};
    if (!in.is_open()) throw std::runtime_error(fmt::format("Could not open input file {}", path.string()));

    for (std::string line; std::getline(in, line);) {
        audit << fmt::format("{}:{} >> {}\n", state_0.file_stack.back().file.string(), state_0.file_stack.back().line_number, line);
        const auto parsed_directive = parse_directive(line);
        if (parsed_directive.has_value()) {
            const auto [directive, past_directive] = *parsed_directive;
            if (directive == include_directive || directive == includesilent_directive) {
                std::string_view included_file{strip_ws(past_directive)};
                process_file(state_0.prefix / included_file, out, audit, state_0);
            } else if (directive == fileprefix_directive) {
                std::string_view prefix{strip_ws(past_directive)};
                state_0.prefix = prefix;
            } else if (directive == nosilent_directive) {
                /* Just ignore */
            } else {
                throw std::runtime_error{fmt::format("Unknown directive {}", directive)};
            }
        } else {
            out << line << '\n';
            audit << fmt::format("{} << {}\n", output_file_name, line);
        }
        ++state_0.file_stack.back().line_number;
    }
    state_0.file_stack.pop_back();
}

int main(const int argc, const char *const argv[])
{
    std::ofstream out{std::filesystem::path{output_file_name}};
    if (!out.is_open()) throw std::runtime_error(fmt::format("Could not open output file {}", output_file_name));

    std::ofstream audit{std::filesystem::path{audit_file_name}};
    if (!audit.is_open()) throw std::runtime_error(fmt::format("Could not open input file {}", audit_file_name));

    state state_0;

    process_file(input_file_name, out, audit, state_0);

    return 0;
}
