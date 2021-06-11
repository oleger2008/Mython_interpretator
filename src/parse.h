#pragma once

#include <memory>
#include <stdexcept>

namespace parse {
class Lexer;
}  // namespace parse

namespace runtime {
class Executable;
}  // namespace runtime

namespace parse {
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::unique_ptr<runtime::Executable> ParseProgram(parse::Lexer& lexer);
}  // namespace parse
