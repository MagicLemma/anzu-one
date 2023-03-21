#include "lexer.hpp"
#include "object.hpp"
#include "utility/print.hpp"

#include <fstream>

namespace anzu {
namespace {

template <typename... Args>
[[noreturn]] void lexer_error(
    std::int64_t lineno, std::int64_t col, std::string_view msg, Args&&... args)
{
    const auto formatted_msg = std::format(msg, std::forward<Args>(args)...);
    anzu::print("[ERROR] ({}:{}) {}\n", lineno, col, formatted_msg);
    std::exit(1);
}

}

auto valid(const scanner& ctx) -> bool
{
    return ctx.curr != ctx.end;
}

auto peek(const scanner& ctx) -> char
{
    return *ctx.curr;
}

auto peek_next(const scanner& ctx) -> char
{
    if (!valid(ctx)) return '\0';
    return *std::next(ctx.curr);
}

auto advance(scanner& ctx) -> char
{
    ++ctx.col;
    return *(ctx.curr++);
}

auto match(scanner& ctx, std::string_view expected) -> bool
{
    auto original_curr = ctx.curr; // so we can roll back if we dont match
    for (char c : expected) {
        if (!valid(ctx)) {
            ctx.curr = original_curr;
            return false;
        }
        if (peek(ctx) != c) {
            ctx.curr = original_curr;
            return false;
        }
        advance(ctx);
    }
    return true;
}

// TODO: We can make this more efficient, but it's fine for now
auto identifier_type(const scanner& ctx) -> token_type
{
    const auto token = std::string_view{ctx.start, ctx.curr};
    if (token == "assert")   return token_type::kw_assert;
    if (token == "bool")     return token_type::kw_bool;
    if (token == "break")    return token_type::kw_break;
    if (token == "char")     return token_type::kw_char;
    if (token == "continue") return token_type::kw_continue;
    if (token == "default")  return token_type::kw_default;
    if (token == "delete")   return token_type::kw_delete;
    if (token == "else")     return token_type::kw_else;
    if (token == "f64")      return token_type::kw_f64;
    if (token == "false")    return token_type::kw_false;
    if (token == "for")      return token_type::kw_for;
    if (token == "fn")       return token_type::kw_function;
    if (token == "i32")      return token_type::kw_i32;
    if (token == "i64")      return token_type::kw_i64;
    if (token == "if")       return token_type::kw_if;
    if (token == "import")   return token_type::kw_import;
    if (token == "in")       return token_type::kw_in;
    if (token == "loop")     return token_type::kw_loop;
    if (token == "new")      return token_type::kw_new;
    if (token == "null")     return token_type::kw_null;
    if (token == "return")   return token_type::kw_return;
    if (token == "sizeof")   return token_type::kw_sizeof;
    if (token == "struct")   return token_type::kw_struct;
    if (token == "true")     return token_type::kw_true;
    if (token == "typeof")   return token_type::kw_typeof;
    if (token == "u64")      return token_type::kw_u64;
    if (token == "while")    return token_type::kw_while;
    if (token == "(")        return token_type::left_paren;
    if (token == ")")        return token_type::right_paren;
    if (token == "{")        return token_type::left_brace;
    if (token == "}")        return token_type::right_brace;
    if (token == ";")        return token_type::semicolon;
    if (token == ",")        return token_type::comma;
    if (token == ".")        return token_type::dot;
    if (token == "-")        return token_type::minus;
    if (token == "+")        return token_type::plus;
    if (token == "/")        return token_type::slash;
    if (token == "*")        return token_type::star;
    if (token == "!=")       return token_type::bang_equal;
    if (token == "!")        return token_type::bang;
    if (token == "==")       return token_type::equal_equal;
    if (token == "=")        return token_type::equal;
    if (token == "<=")       return token_type::less_equal;
    if (token == "<")        return token_type::less;
    if (token == ">=")       return token_type::greater_equal;
    if (token == ">")        return token_type::greater;
    if (token == "&")        return token_type::ampersand;
    if (token == "&&")       return token_type::ampersand_ampersand;
    if (token == ":=")       return token_type::colon_equal;
    if (token == ":")        return token_type::colon;
    if (token == "[")        return token_type::left_bracket;
    if (token == "]")        return token_type::right_bracket;
    if (token == "%")        return token_type::percent;
    if (token == "||")       return token_type::bar_bar;
    if (token == "|")        return token_type::bar;
    if (token == "->")       return token_type::arrow;
    return token_type::identifier;
}

auto skip_whitespace(scanner& ctx) -> void
{
    while (valid(ctx)) {
        const char c = peek(ctx);
        switch (c) {
            case ' ':
            case '\r':
            case '\t': {
                advance(ctx);
            } break;
            case '\n': {
                advance(ctx);
                ++ctx.line;
                ctx.col = 1;
            } break;
            case '#': {
                while (valid(ctx) && peek(ctx) != '\n') {
                    advance(ctx);
                }
            } break;
            default: {
                return;
            }
        }
    }
}

auto make_token(const scanner& ctx, token_type type) -> token
{
    const auto text = std::string_view{ctx.start, ctx.curr};

    // ctx.col is currently the end of the token, hence the offset to the front
    return token{ .text=text, .line=ctx.line, .col=(ctx.col - text.size()), .type=type };
}

auto make_identifier(scanner& ctx) -> token
{
    while (std::isalpha(peek(ctx)) || std::isdigit(peek(ctx)) || peek(ctx) == '_') advance(ctx);
    return make_token(ctx, identifier_type(ctx));
}

auto make_number(scanner& ctx) -> token
{
    while (std::isdigit(peek(ctx))) advance(ctx);

    // look for any fractional part
    if (peek(ctx) == '.' && std::isdigit(peek_next(ctx))) {
        advance(ctx); // consume the '.'
        while (std::isdigit(peek(ctx))) advance(ctx);
        return make_token(ctx, token_type::float64);
    }

    if (match(ctx, "u64")) return make_token(ctx, token_type::uint64);
    if (match(ctx, "u"))   return make_token(ctx, token_type::uint64);
    if (match(ctx, "i32")) return make_token(ctx, token_type::int32);
    if (match(ctx, "i64")) return make_token(ctx, token_type::int64); // for completeness
    return make_token(ctx, token_type::int64);
}

auto make_literal(scanner& ctx, char delimiter, token_type tt) -> token
{
    while (valid(ctx) && peek(ctx) != delimiter) {
        if (peek(ctx) == '\n') {
            ctx.line++;
            ctx.col = 1;
        }
        advance(ctx);
    }

    if (!valid(ctx)) lexer_error(ctx.line, ctx.col, "Unterminated string");
    advance(ctx); // closing quote

    auto tok = make_token(ctx, tt);
    tok.text.remove_prefix(1); // remove leading "
    tok.text.remove_suffix(1); // remove trailing "
    return tok;
}

auto make_string(scanner& ctx) -> token
{
    return make_literal(ctx, '"', token_type::string);
}

auto make_char(scanner& ctx) -> token
{
    const auto tok = make_literal(ctx, '\'', token_type::character);
    if (const auto size = tok.text.size(); size != 1) {
        lexer_error(ctx.line, ctx.col, "Char literal is not one character! Got '{}' ({})", tok.text, size);
    }
    return tok;
}

auto read_file(const std::filesystem::path& file) -> std::unique_ptr<std::string>
{
    std::ifstream ifs{file};
    if (!ifs) {
        lexer_error(0, 0, "Could not find module {}\n", file.string());
    }

    using iter = std::istreambuf_iterator<char>;
    return std::make_unique<std::string>(iter{ifs}, iter{});
}

scanner::scanner(std::string_view source_code)
    : start{source_code.begin()}
    , curr{source_code.begin()}
    , end{source_code.end()}
{
}

auto scanner::get_token() -> token
{
    auto& ctx = *this;
    skip_whitespace(ctx);
    if (!valid(ctx)) return make_token(ctx, token_type::eof);

    ctx.start = ctx.curr;
    
    const auto c = advance(ctx);
    if (std::isalpha(c)) return make_identifier(ctx);
    if (std::isdigit(c)) return make_number(ctx);

    switch (c) {
        case '(': return make_token(ctx, token_type::left_paren);
        case ')': return make_token(ctx, token_type::right_paren);
        case '{': return make_token(ctx, token_type::left_brace);
        case '}': return make_token(ctx, token_type::right_brace);
        case '[': return make_token(ctx, token_type::left_bracket);
        case ']': return make_token(ctx, token_type::right_bracket);
        case ';': return make_token(ctx, token_type::semicolon);
        case ',': return make_token(ctx, token_type::comma);
        case '.': return make_token(ctx, token_type::dot);
        case '-': return make_token(ctx,
            match(ctx, ">") ? token_type::arrow : token_type::minus);
        case '+': return make_token(ctx, token_type::plus);
        case '/': return make_token(ctx, token_type::slash);
        case '*': return make_token(ctx, token_type::star);
        case '%': return make_token(ctx, token_type::percent);
        case '!': return make_token(ctx,
            match(ctx, "=") ? token_type::bang_equal : token_type::bang);
        case '=': return make_token(ctx,
            match(ctx, "=") ? token_type::equal_equal : token_type::equal);
        case '<': return make_token(ctx,
            match(ctx, "=") ? token_type::less_equal : token_type::less);
        case '>': return make_token(ctx,
            match(ctx, "=") ? token_type::greater_equal : token_type::greater);
        case ':': return make_token(ctx,
            match(ctx, "=") ? token_type::colon_equal : token_type::colon);
        case '|': return make_token(ctx,
            match(ctx, "|") ? token_type::bar_bar : token_type::bar);
        case '&': return make_token(ctx,
            match(ctx, "&") ? token_type::ampersand_ampersand : token_type::ampersand);
        case '\'':
            return make_char(ctx);
        case '"':
            return make_string(ctx);
    }

    lexer_error(ctx.line, ctx.col, "Unknown token");
}

auto lex_print(std::string_view source_code) -> void
{
    auto ctx = scanner{source_code};
    for (auto token = ctx.get_token(); token.type != token_type::eof; token = ctx.get_token()) {
        const auto text = std::format("'{}'", token.text);
        anzu::print("{:<15} - {:<20} {:<5} {:<5}\n", to_string(token.type), text, token.line, token.col);
    }
}

tokenstream::tokenstream(std::string_view source_code)
    : d_ctx{scanner(source_code)}
    , d_curr{d_ctx.get_token()}
    , d_next{d_ctx.get_token()}
{}

auto tokenstream::consume_maybe(token_type tt) -> bool
{
    if (valid() && curr().type == tt) {
        consume();
        return true;
    }
    return false;
}

auto tokenstream::consume_only(token_type tt, std::source_location loc) -> token
{
    if (!valid()) {
        anzu::print("[ERROR] (EOF) expected '{}'\n", tt);
        std::exit(1);
    }
    if (curr().type != tt) {
        curr().error("expected '{}', got '{}' [PARSER:{}]\n", tt, curr().type, loc.line());
    }
    return consume();
}

auto tokenstream::consume_i64() -> std::int64_t
{
    if (!valid()) {
        anzu::print("[ERROR] (EOF) expected an int\n");
        std::exit(1);
    }
    if (curr().type != token_type::int64) {
        curr().error("expected {}, got '{}'\n", token_type::int64, curr().type);
    }
    return std::stoll(std::string{consume().text}); // todo - use from_chars
}

auto tokenstream::consume_u64() -> std::uint64_t
{
    if (!valid()) {
        anzu::print("[ERROR] (EOF) expected a uint\n");
        std::exit(1);
    }
    if (curr().type != token_type::uint64) {
        curr().error("expected u64, got '{}'\n", token_type::uint64, curr().type);
    }
    return std::stoull(std::string{consume().text}); // todo - use from_chars
}

auto tokenstream::peek(token_type tt) -> bool
{
    return valid() && curr().type == tt;
}

auto tokenstream::peek_next(token_type tt) -> bool
{
    return has_next() && next().type == tt;
}

}