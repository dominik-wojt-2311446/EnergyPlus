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
#include <algorithm>
#include <cstddef>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
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
constexpr std::string_view set1_directive = "set1";
constexpr std::string_view if_directive = "if";
constexpr std::string_view ifdef_directive = "ifdef";
constexpr std::string_view ifndef_directive = "ifndef";
constexpr std::string_view else_directive = "else";
constexpr std::string_view elseif_directive = "elseif";
constexpr std::string_view endif_directive = "endif";

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

enum class character_category
{
    white_space,
    left_square_bracket,
    right_square_bracket,
    hash,
    others
};

character_category get_character_category(const char c)
{
    if (white_space.find(c) != std::string_view::npos) return character_category::white_space;
    if (c == '[') return character_category::left_square_bracket;
    if (c == ']') return character_category::right_square_bracket;
    if (c == '#') return character_category::hash;
    return character_category::others;
}

std::vector<std::string_view> split_line(std::string_view line)
{
    std::vector<std::string_view> result;
    std::string_view::iterator current_begin = line.begin();
    while (current_begin != line.end()) {
        character_category category = get_character_category(*current_begin);
        const std::string_view::iterator current_end =
            std::find_if_not(current_begin + 1, line.end(), [&](const auto c) { return get_character_category(c) == category; });
        result.emplace_back(current_begin, current_end - current_begin);
        current_begin = current_end;
    }
    return result;
}

std::vector<std::string_view> filter_white_space(std::vector<std::string_view> strings)
{
    std::vector<std::string_view> result;
    std::copy_if(strings.begin(), strings.end(), std::back_inserter(result), [](const auto s) {
        return !s.empty() && get_character_category(s.front()) != character_category::white_space;
    });
    return result;
}

void process_file(const std::filesystem::path &path, std::ofstream &out, std::ofstream &audit, state &state_0)
{
    state_0.file_stack.push_back({path, 1});

    std::ifstream in{path};
    if (!in.is_open()) {
        throw std::runtime_error(fmt::format("Could not open input file {}", path.string()));
    }

    for (std::string line; std::getline(in, line);) {
        audit << fmt::format("{}:{} >> {}\n", state_0.file_stack.back().file.string(), state_0.file_stack.back().line_number, line);

        const std::vector<std::string_view> split = split_line(line);
        const std::vector<std::string_view> non_ws = filter_white_space(split);
        if (non_ws.size() >= 2 && non_ws[0] == "##") {
            const std::string_view directive = non_ws[1];
            if (directive == include_directive || directive == includesilent_directive) {
                if (non_ws.size() < 3) {
                    throw std::runtime_error(fmt::format("Missing argument for directive in {}", line));
                }
                const std::string_view included_file{non_ws[2]};
                process_file(state_0.prefix / included_file, out, audit, state_0);
            } else if (directive == fileprefix_directive) {
                std::string_view prefix{non_ws.size() > 2 ? non_ws[2] : std::string_view{}};
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
