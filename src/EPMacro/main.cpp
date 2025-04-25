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

struct if_state
{
    bool true_case_active{false};
    bool true_case_found{false};
};

struct state
{
    std::vector<file_pointer> file_stack;
    std::filesystem::path prefix;
    std::map<std::string, macro_definition> macros;
    std::vector<if_state> if_states;
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
    comma,
    others
};

character_category get_character_category(const char c)
{
    if (white_space.find(c) != std::string_view::npos) return character_category::white_space;
    if (c == '[') return character_category::left_square_bracket;
    if (c == ']') return character_category::right_square_bracket;
    if (c == '#') return character_category::hash;
    if (c == ',') return character_category::comma;
    return character_category::others;
}

character_category get_character_category(const std::string_view s)
{
    if (s.empty()) {
        throw std::runtime_error("Trying to get character category for an empty string view.");
    }
    return get_character_category(s.front());
}

using vector_sv = std::vector<std::string_view>;

vector_sv split_line(std::string_view line)
{
    vector_sv result;
    std::string_view::iterator current_begin = line.begin();
    while (current_begin != line.end()) {
        character_category category = get_character_category(*current_begin);
        const std::string_view::iterator current_end = [&]() {
            /* TODO: handle quotes */
            if (category == character_category::left_square_bracket || category == character_category::right_square_bracket) {
                return current_begin + 1;
            } else {
                return std::find_if_not(current_begin + 1, line.end(), [&](const auto c) { return get_character_category(c) == category; });
            }
        }();
        result.emplace_back(current_begin, current_end - current_begin);
        current_begin = current_end;
    }
    return result;
}

vector_sv filter_white_space(const vector_sv &strings)
{
    vector_sv result;
    std::copy_if(strings.begin(), strings.end(), std::back_inserter(result), [](const auto s) {
        return get_character_category(s) != character_category::white_space;
    });
    return result;
}

vector_sv::const_iterator find_non_ws(const vector_sv::const_iterator i, const vector_sv::const_iterator end)
{
    return std::find_if(i, end, [](const auto s) { //
        return get_character_category(s) != character_category::white_space;
    });
}

vector_sv::const_iterator next_non_ws(const vector_sv::const_iterator i, const vector_sv::const_iterator end)
{
    if (i == end) {
        return end;
    }
    return find_non_ws(std::next(i), end);
}

vector_sv read_argument_list(const vector_sv::const_iterator begin, const vector_sv::const_iterator end)
{
    vector_sv result;
    bool comma_read = false;
    std::for_each(begin, end, [&](const auto s) {
        const auto category = get_character_category(s);
        if (category == character_category::others) {
            result.push_back(s);
            comma_read = false;
            return;
        }
        if (category == character_category::comma) {
            if (comma_read) {
                throw std::runtime_error("Double comma found in argument list");
            }
            comma_read = true;
            return;
        }
        if (category == character_category::white_space) {
            return;
        }
        throw std::runtime_error("Unexpected category detected on macro argument list");
    });
    if (comma_read) {
        throw std::runtime_error("Trailing comma found in argument list");
    }
    return result;
}

std::optional<std::tuple<vector_sv::const_iterator, std::string_view>> read_directive(const vector_sv::const_iterator begin,
                                                                                      vector_sv::const_iterator end)
{
    const auto hash_hash_iter = find_non_ws(begin, end);
    if (hash_hash_iter == end || *hash_hash_iter != "##") return {};
    const auto directive_iter = next_non_ws(hash_hash_iter, end);
    if (directive_iter == end) {
        throw std::runtime_error("No directive found after ##");
    }
    return {{next_non_ws(directive_iter, end), *directive_iter}};
}

void process_file(const std::filesystem::path &path, std::ofstream &out, std::ofstream &audit, state &state_0);

void process_include(
    const vector_sv::const_iterator begin, const vector_sv::const_iterator end, std::ofstream &out, std::ofstream &audit, state &state_0)
{
    const auto file_name_iter = find_non_ws(begin, end);
    if (file_name_iter == end) {
        throw std::runtime_error(fmt::format("Missing argument for include"));
    }
    const std::string_view file_name{*file_name_iter};
    process_file(state_0.prefix / file_name, out, audit, state_0);
}

void process_fileprefix(const vector_sv::const_iterator begin, const vector_sv::const_iterator end, state &state_0)
{
    const auto fileprefix_iter = find_non_ws(begin, end);
    const std::string_view fileprefix{fileprefix_iter != end ? *fileprefix_iter : std::string_view{}};
    state_0.prefix = fileprefix;
}

void process_set1(const vector_sv::const_iterator begin, const vector_sv::const_iterator end, state &state_0)
{
    auto current_iter = begin;
    if (current_iter == end) {
        throw std::runtime_error("No identifier for set1 found");
    }
    const auto identifier_iter = current_iter;
    const auto identifier = *identifier_iter;

    macro_definition md;
    current_iter = next_non_ws(current_iter, end);
    if (current_iter != end && get_character_category(*current_iter) == character_category::left_square_bracket) {
        const auto left_bracket_iter = current_iter;
        const auto right_bracket_iter = std::find_if(std::next(left_bracket_iter), end, [](const auto s) { //
            return get_character_category(s) == character_category::right_square_bracket;
        });
        if (right_bracket_iter == end) {
            throw std::runtime_error("No matching ] found for open [");
        }
        const vector_sv arugment_list = read_argument_list(std::next(left_bracket_iter), right_bracket_iter);
        std::transform(arugment_list.begin(), arugment_list.end(), std::back_inserter(md.arguments), [](const auto s) { //
            return std::string{s};
        });
        current_iter = next_non_ws(right_bracket_iter, end);
    }

    const auto definition_begin = current_iter;
    /* TODO: evaluate before assignment */
    /* The definition may be empty (definition_begin == end). */
    std::transform(definition_begin, end, std::back_inserter(md.tokens), [](const auto s) { //
        return std::string{s};
    });
    state_0.macros[std::string{identifier}] = md;
}

void process_ifdef(const vector_sv::const_iterator begin, const vector_sv::const_iterator end, state &state_0)
{
    auto current_iter = begin;
    if (current_iter == end) {
        throw std::runtime_error("No identifier for ifdef found");
    }
    const auto identifier_iter = current_iter;
    const auto identifier = *identifier_iter;

    const bool defined = state_0.macros.find(std::string{identifier}) != state_0.macros.end();
    state_0.if_states.push_back({defined, defined});
}

void process_else(const vector_sv::const_iterator begin, const vector_sv::const_iterator end, state &state_0)
{
    if (state_0.if_states.size() == 0) {
        throw std::runtime_error("unexpected else");
    }
    auto &current_if_state = state_0.if_states.back();
    if (current_if_state.true_case_found) {
        current_if_state.true_case_active = false;
    } else {
        current_if_state.true_case_active = true;
        current_if_state.true_case_found = true;
    }
}

void process_endif(state &state_0)
{
    if (state_0.if_states.size() == 0) {
        throw std::runtime_error("unexpected else");
    }
    state_0.if_states.pop_back();
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
        auto current_iter = split.begin();
        const auto end = split.end();
        const auto maybe_directive = read_directive(current_iter, end);
        const bool active = state_0.if_states.empty() || state_0.if_states.back().true_case_active;

        if (maybe_directive.has_value()) {
            current_iter = std::get<0>(*maybe_directive);
            const std::string_view directive = std::get<1>(*maybe_directive);
            if (active) {
                if (directive == include_directive || directive == includesilent_directive) {
                    process_include(current_iter, end, out, audit, state_0);
                } else if (directive == fileprefix_directive) {
                    process_fileprefix(current_iter, end, state_0);
                } else if (directive == nosilent_directive) {
                    /* Just ignore */
                } else if (directive == set1_directive) {
                    process_set1(current_iter, end, state_0);
                }
            }
            if (directive == ifdef_directive) {
                process_ifdef(current_iter, end, state_0);
            } else if (directive == else_directive) {
                process_else(current_iter, end, state_0);
            } else if (directive == endif_directive) {
                process_endif(state_0);
            }
        } else {
            if (active) {
                out << line << '\n';
                audit << fmt::format("{} << {}\n", output_file_name, line);
            }
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
