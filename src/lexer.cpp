#include "lexer.h"

#include <algorithm>
#include <iostream>

using namespace std;

namespace parse {

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
        return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}

Lexer::Lexer(std::istream& input) {
    ReadInput(input);
    iter_ = tokens_.begin();
}

const Token& Lexer::CurrentToken() const {
    return *iter_;
}

Token Lexer::NextToken() {
    if (next(iter_) != tokens_.end()) {
        ++iter_;
    }
    return *iter_;
}

void Lexer::ReadInput(std::istream& input) {
    using namespace std::literals;

    bool is_stream_begin = true;
    bool is_line_begin = true;
    int indent_count = 0;
    char c;
    while (input) {
        c = input.get();
        if (!input.good()) {
            break;
        }
        if (is_stream_begin) {
            is_stream_begin = false;
            if (c == ' ') {
                throw LexerError("Space in the begining of istream"s);
            }
        }
        if (is_line_begin) {
            if (c == '\n') {
                continue;
            }
            if (c == '#') {
                SkipComment(input);
                continue;
            }
            if (c == ' ') {
                input.putback(c);
                int space_count = IsNonemptyLine(input);
                if (!space_count) {
                    continue;
                } else {
                    if (tokens_.empty()) {
                        throw LexerError("Trying to indent before any token appears"s);
                    }
                    is_line_begin = false;
                    // проверка на наличие значимых токенов, что токены не пустые
                    AdjustIndentCount(indent_count, space_count);
                    continue;
                }
            } else {
                input.putback(c);
                is_line_begin = false;
                AdjustIndentCount(indent_count, 0);
                continue;
            }

        }
        if (std::isdigit(c)) {
            input.putback(c);
            LoadNumber(input);
            continue;
        }
        if (c == '\'' || c == '\"') {
            input.putback(c);
            LoadString(input);
            continue;
        }
        if (c == '_' || std::isalpha(c)) {
            input.putback(c);
            LoadKeyWordOrId(input);
            continue;
        }
        if(operations_.find(c) != std::string::npos) {
            tokens_.emplace_back(token_type::Char{c});
            continue;
        }
        switch(c) {
            case '=' :
                if (input.peek() == '=') {
                    input.get(c);
                    tokens_.emplace_back(token_type::Eq{});
                } else {
                    tokens_.emplace_back(token_type::Char{c});
                }
                continue;
            case '!' :
                if (input.peek() == '=') {
                    input.get(c);
                    tokens_.emplace_back(token_type::NotEq{});
                }
                continue;
            case '<' :
                if (input.peek() == '=') {
                    input.get(c);
                    tokens_.emplace_back(token_type::LessOrEq{});
                } else {
                    tokens_.emplace_back(token_type::Char{c});
                }
                continue;
            case '>' :
                if (input.peek() == '=') {
                    input.get(c);
                    tokens_.emplace_back(token_type::GreaterOrEq{});
                } else {
                    tokens_.emplace_back(token_type::Char{c});
                }
                continue;

            case '#' :
                SkipComment(input);
                if (input.good()) {
                    input.putback('\n');
                }
                continue;
            case '\n' :
                tokens_.emplace_back(token_type::Newline{});
                is_line_begin = true;
                continue;
            case ' ' :
                continue;


            default:
                tokens_.emplace_back(token_type::Char{c});
        }

    }

    if (!tokens_.empty() && !tokens_.back().Is<token_type::Newline>()) {
        tokens_.emplace_back(token_type::Newline{});
    }
    if (indent_count > 0) {
        while (indent_count) {
            --indent_count;
            tokens_.emplace_back(token_type::Dedent{});
        }
    }
    tokens_.emplace_back(token_type::Eof{});
}

int Lexer::IsNonemptyLine(std::istream& input) {
    char c;
    int space_count = 0;
    while (input) {
        c = input.get();
        if (c != ' ') {
            break;
        }
        ++space_count;
    }
    if (!input) {
        return 0;
    }
    switch(c) {
        case '\n' :
            return 0;
        case '#' :
            SkipComment(input);
            return 0;
        default:
            input.putback(c);
    }

    return space_count;
}

void Lexer::AdjustIndentCount(int& indent_count, int space_count) {
    using namespace std::literals;
    if (space_count % 2) {
        throw LexerError("Odd space count in indent"s);
    } else {
        int diff = space_count / 2 - indent_count;
        if (diff > 1) {
            throw LexerError("Trying to make more indent than needs"s);
        } else if (diff == 1) {
            ++indent_count;
            tokens_.emplace_back(token_type::Indent{});
        } else if (diff < 0) { // diff == 0 намеренно пропущен, т.к. в этом случае ничего делать не нужно
            while (diff) {
                --indent_count;
                tokens_.emplace_back(token_type::Dedent{});
                ++diff;
            }
            if (indent_count < 0) {
                throw LexerError("indent_count < 0"s);
            }
        }
    }
}

void Lexer::LoadNumber(std::istream& input) {
    using namespace std::literals;
    std::string parsed_num;

    // Считывает в parsed_num очередной символ из input
    auto read_char = [&parsed_num, &input] {
        parsed_num += static_cast<char>(input.get());
        if (!input) {
            throw LexerError("Failed to read number from stream"s);
        }
    };

    // Считывает одну или более цифр в parsed_num из input
    auto read_digits = [&input, read_char] {
        if (!std::isdigit(input.peek())) {
            throw LexerError("A digit is expected"s);
        }
        while (std::isdigit(input.peek())) {
            read_char();
        }
    };

    // Парсим целую часть числа
    if (input.peek() == '0') {
        read_char();
        // После 0 не могут идти другие цифры
    } else {
        read_digits();
    }

    try {
        tokens_.emplace_back(token_type::Number{std::stoi(parsed_num)});
    } catch (...) {
        throw LexerError("Unknown error while stoi(parsed_num)");
    }
}

void Lexer::LoadString(std::istream& input) {
    using namespace std::literals;
    char quote;
    input >> quote;
    auto it = std::istreambuf_iterator<char>(input);
    auto end = std::istreambuf_iterator<char>();
    std::string s;
    while (true) {
        if (it == end) {
            throw LexerError("Eof after opened double quote"s);
        }
        const char ch = *it;
        if (ch == quote) {
            ++it;
            break;
        } else if (ch == '\\') {
            ++it;
            if (it == end) {
                throw LexerError("Eof after opened double quote");
            }
            const char escaped_char = *(it);
            switch (escaped_char) {
                case 'n':
                    s.push_back('\n');
                    break;
                case 't':
                    s.push_back('\t');
                    break;
                case 'r':
                    s.push_back('\r');
                    break;
                case '"':
                    s.push_back('"');
                    break;
                case '\'':
                    s.push_back('\'');
                    break;
                case '\\':
                    s.push_back('\\');
                    break;
                default:
                    throw LexerError("Unrecognized escape sequence \\"s + escaped_char);
            }
        } else {
            s.push_back(ch);
        }
        ++it;
    }
    tokens_.emplace_back(token_type::String{s});
}

void Lexer::LoadKeyWordOrId(std::istream& input) {
    using namespace std::literals;
    std::string s;
    {
        char c;
        while(input) {
            c = input.get();
            if (!input.good()) {
                break;
            }
            if (c == ' ' || c == '\n' || c == '#') {
                input.putback(c);
                break;
            }
            if (!std::isalpha(c) && !std::isdigit(c) && c != '_') {
                input.putback(c);
                break;
            }
            s.push_back(c);
        }
    }

    if (const auto iter = std::find(key_words_.begin(), key_words_.end(), s); iter != key_words_.end()) {
        switch(static_cast<KeyWords>(std::distance(key_words_.begin(), iter))) {
            case KeyWords::CLASS :
                tokens_.emplace_back(token_type::Class{});
                break;
            case KeyWords::RETURN :
                tokens_.emplace_back(token_type::Return{});
                break;
            case KeyWords::IF :
                tokens_.emplace_back(token_type::If{});
                break;
            case KeyWords::ELSE :
                tokens_.emplace_back(token_type::Else{});
                break;
            case KeyWords::DEF :
                tokens_.emplace_back(token_type::Def{});
                break;
            case KeyWords::PRINT :
                tokens_.emplace_back(token_type::Print{});
                break;
            case KeyWords::AND :
                tokens_.emplace_back(token_type::And{});
                break;
            case KeyWords::OR :
                tokens_.emplace_back(token_type::Or{});
                break;
            case KeyWords::NOT :
                tokens_.emplace_back(token_type::Not{});
                break;
            case KeyWords::NONE :
                tokens_.emplace_back(token_type::None{});
                break;
            case KeyWords::TRUE :
                tokens_.emplace_back(token_type::True{});
                break;
            case KeyWords::FALSE :
                tokens_.emplace_back(token_type::False{});
                break;
        }
    } else {
        tokens_.emplace_back(token_type::Id{s});
    }
}

void Lexer::SkipComment(std::istream& input) {
    char c;
    while (input) {
        c = input.get();
        if (c == '\n') {
            break;
        }
    }
}

}  // namespace parse
