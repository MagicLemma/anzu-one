#include "parse_expression.hpp"

namespace anzu {
namespace {

enum precedence : int {
  PREC_NONE,
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . () [] !() @ const &
  PREC_SCOPE,       // ::
  PREC_PRIMARY
};

using prefix_func = auto(*) (tokenstream&) -> node_expr_ptr;
using midfix_func = auto(*) (tokenstream&, const node_expr_ptr&) -> node_expr_ptr;

struct parse_rule
{
    prefix_func prefix;
    midfix_func midfix;
    precedence  prec;
};

template <typename Inner>
auto new_node(const token& tok) -> std::tuple<node_expr_ptr, Inner&>
{
    auto node = std::make_shared<node_expr>();
    auto& inner = node->emplace<Inner>();
    inner.token = tok;
    return {node, std::ref(inner)};
}

auto parse_precedence(tokenstream& tokens, precedence prec) -> node_expr_ptr;
auto get_rule(token_type tt) -> const parse_rule*;

template <typename ExprType, token_type TokenType>
auto parse_number(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(TokenType);
    auto [node, inner] = new_node<ExprType>(token);
    auto text = token.text;

    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), inner.value);
    token.assert(ec == std::errc{}, "cannot convert '{}' to '{}'\n", text, TokenType);
    return node;
}

auto parse_i32(tokenstream& tokens) -> node_expr_ptr
{
    return parse_number<node_literal_i32_expr, token_type::int32>(tokens);
}

auto parse_i64(tokenstream& tokens) -> node_expr_ptr
{
    return parse_number<node_literal_i64_expr, token_type::int64>(tokens);
}

auto parse_u64(tokenstream& tokens) -> node_expr_ptr
{
    return parse_number<node_literal_u64_expr, token_type::uint64>(tokens);
}

auto parse_f64(tokenstream& tokens) -> node_expr_ptr
{
    return parse_number<node_literal_f64_expr, token_type::float64>(tokens);
}

auto parse_char(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::character);
    auto [node, inner] = new_node<node_literal_char_expr>(token);
    inner.value = token.text.front();
    return node;
}

auto parse_string(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::string);
    auto [node, inner] = new_node<node_literal_string_expr>(token);
    inner.value = token.text;
    return node;
}

auto parse_true(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_true);
    auto [node, inner] = new_node<node_literal_bool_expr>(token);
    inner.value = true;
    return node;
}

auto parse_false(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_false);
    auto [node, inner] = new_node<node_literal_bool_expr>(token);
    inner.value = false;
    return node;
}

auto parse_null(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_null);
    auto [node, inner] = new_node<node_literal_null_expr>(token);
    return node;
}

auto parse_nullptr(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_nullptr);
    auto [node, inner] = new_node<node_literal_nullptr_expr>(token);
    return node;
}

auto parse_name(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume();
    auto [node, inner] = new_node<node_name_expr>(token);
    inner.name = token.text;
    return node;
}

auto parse_grouping(tokenstream& tokens) -> node_expr_ptr
{
    tokens.consume_only(token_type::left_paren);
    const auto node = parse_expression(tokens);
    tokens.consume_only(token_type::right_paren);
    return node;
}

auto parse_unary(tokenstream& tokens) -> node_expr_ptr
{
    const auto op = tokens.consume();
    auto expr = parse_precedence(tokens, precedence::PREC_UNARY);

    auto [node, inner] = new_node<node_unary_op_expr>(op);
    inner.expr = expr;
    return node;
}

auto parse_typeof(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_typeof);
    tokens.consume_only(token_type::left_paren);
    auto [node, inner] = new_node<node_typeof_expr>(token);
    inner.expr = parse_expression(tokens);
    tokens.consume_only(token_type::right_paren);
    return node;
}

auto parse_sizeof(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_sizeof);
    tokens.consume_only(token_type::left_paren);
    auto [node, inner] = new_node<node_sizeof_expr>(token);
    inner.expr = parse_expression(tokens);
    tokens.consume_only(token_type::right_paren);
    return node;
}

auto parse_array(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::left_bracket);
    const auto first = parse_expression(tokens);

    if (tokens.consume_maybe(token_type::semicolon)) {
        auto [node, inner] = new_node<node_repeat_array_expr>(token);
        inner.value = first;
        inner.size = std::get<node_literal_u64_expr>(*parse_u64(tokens)).value; // TODO: store the expr here
        tokens.consume_only(token_type::right_bracket);
        return node;
    } else {
        auto [node, inner] = new_node<node_array_expr>(token);
        inner.elements.push_back(first);
        if (!tokens.consume_maybe(token_type::right_bracket)) {
            tokens.consume_only(token_type::comma);
            tokens.consume_comma_separated_list(token_type::right_bracket, [&] {
                inner.elements.push_back(parse_expression(tokens));
            });
        }
        return node;
    }
}

auto parse_func_ptr(tokenstream& tokens) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_function);
    tokens.consume_only(token_type::left_paren);
    auto [node, inner] = new_node<node_function_ptr_type_expr>(token);
    tokens.consume_comma_separated_list(token_type::right_paren, [&] {
        inner.params.push_back(parse_expression(tokens));
    });
    tokens.consume_only(token_type::arrow); // TODO: allow for this to be optional
    inner.return_type = parse_expression(tokens);
    return node;
}

auto parse_binary(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    const auto op = tokens.consume();
    auto rule = get_rule(op.type);
    auto right = parse_precedence(tokens, (precedence)(rule->prec + 1));

    auto [node, inner] = new_node<node_binary_op_expr>(op);
    inner.lhs = left;
    inner.rhs = right;
    return node;
}

auto parse_call(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    auto [node, inner] = new_node<node_call_expr>(tokens.curr());
    inner.expr = left;
    if (tokens.consume_maybe(token_type::bang)) {
        tokens.consume_only(token_type::left_paren);
        tokens.consume_comma_separated_list(token_type::right_paren, [&] {
            inner.template_args.push_back(parse_expression(tokens));
        });
    }
    tokens.consume_only(token_type::left_paren);
    tokens.consume_comma_separated_list(token_type::right_paren, [&] {
        inner.args.push_back(parse_expression(tokens));
    });
    
    return node;
}

auto parse_subscript(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    auto node = std::make_shared<node_expr>();
    const auto token = tokens.consume_only(token_type::left_bracket);

    if (tokens.consume_maybe(token_type::right_bracket)) {    // x[]
        auto& inner = node->emplace<node_span_expr>();
        inner.token = token;
        inner.expr = left;
        return node;
    }

    auto expr = parse_expression(tokens);
    if (tokens.consume_maybe(token_type::colon)) {            // x[a:b]
        auto& inner = node->emplace<node_span_expr>();
        inner.token = token;
        inner.expr = left;
        inner.lower_bound = expr;
        inner.upper_bound = parse_expression(tokens);
    } else {                                                  // x[i]
        auto& inner = node->emplace<node_subscript_expr>();
        inner.token = token;
        inner.expr = left;
        inner.index = expr;
    }

    tokens.consume_only(token_type::right_bracket);
    return node;
}

auto parse_dot(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::dot);
    const auto name = tokens.consume_only(token_type::identifier);

    if (tokens.peek(token_type::left_paren) || tokens.peek(token_type::bang)) {
        auto [node, inner] = new_node<node_member_call_expr>(token);
        inner.expr = left;
        inner.function_name = std::string{name.text};
        if (tokens.consume_maybe(token_type::bang)) {
            tokens.consume_only(token_type::left_paren);
            tokens.consume_comma_separated_list(token_type::right_paren, [&] {
                inner.template_args.push_back(parse_expression(tokens));
            });
        }
        tokens.consume_only(token_type::left_paren);
        tokens.consume_comma_separated_list(token_type::right_paren, [&] {
            inner.other_args.push_back(parse_expression(tokens));
        });
        return node;
    } else {
        auto [node, inner] = new_node<node_field_expr>(token);
        inner.expr = left;
        inner.field_name = std::string{name.text};
        return node;
    }

}

auto parse_const(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::kw_const);
    auto [node, inner] = new_node<node_const_expr>(token);
    inner.expr = left;
    return node;
}

auto parse_at(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::at);
    auto [node, inner] = new_node<node_deref_expr>(token);
    inner.expr = left;
    return node;
}

auto parse_ampersand(tokenstream& tokens, const node_expr_ptr& left) -> node_expr_ptr
{
    const auto token = tokens.consume_only(token_type::ampersand);
    auto [node, inner] = new_node<node_addrof_expr>(token);
    inner.expr = left;
    return node;
}

auto parse_precedence(tokenstream& tokens, precedence prec) -> node_expr_ptr
{
    const auto token = tokens.curr();
    auto rule = get_rule(token.type);
    token.assert(rule->prefix, "expected an expression");

    auto node = rule->prefix(tokens);
    while (prec <= get_rule(tokens.curr().type)->prec) {
        auto midfix = get_rule(tokens.curr().type)->midfix;
        node = midfix(tokens, node);
    }

    return node;
}

static const auto rules = std::unordered_map<token_type, parse_rule>
{
    {token_type::left_paren,          {parse_grouping, parse_call,      precedence::PREC_CALL}},
    {token_type::bang,                {parse_unary,    parse_call,      precedence::PREC_CALL}},
    {token_type::minus,               {parse_unary,    parse_binary,    precedence::PREC_TERM}},
    {token_type::plus,                {nullptr,        parse_binary,    precedence::PREC_TERM}},
    {token_type::slash,               {nullptr,        parse_binary,    precedence::PREC_FACTOR}},
    {token_type::star,                {nullptr,        parse_binary,    precedence::PREC_FACTOR}},
    {token_type::percent,             {nullptr,        parse_binary,    precedence::PREC_FACTOR}},
    {token_type::int32,               {parse_i32,      nullptr,         precedence::PREC_NONE}},
    {token_type::int64,               {parse_i64,      nullptr,         precedence::PREC_NONE}},
    {token_type::uint64,              {parse_u64,      nullptr,         precedence::PREC_NONE}},
    {token_type::float64,             {parse_f64,      nullptr,         precedence::PREC_NONE}},
    {token_type::character,           {parse_char,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_true,             {parse_true,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_false,            {parse_false,    nullptr,         precedence::PREC_NONE}},
    {token_type::kw_null,             {parse_null,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_nullptr,          {parse_nullptr,  nullptr,         precedence::PREC_NONE}},
    {token_type::string,              {parse_string,   nullptr,         precedence::PREC_NONE}},
    {token_type::equal_equal,         {nullptr,        parse_binary,    precedence::PREC_EQUALITY}},
    {token_type::bang_equal,          {nullptr,        parse_binary,    precedence::PREC_EQUALITY}},
    {token_type::less,                {nullptr,        parse_binary,    precedence::PREC_COMPARISON}},
    {token_type::less_equal,          {nullptr,        parse_binary,    precedence::PREC_COMPARISON}},
    {token_type::greater,             {nullptr,        parse_binary,    precedence::PREC_COMPARISON}},
    {token_type::greater_equal,       {nullptr,        parse_binary,    precedence::PREC_COMPARISON}},
    {token_type::ampersand_ampersand, {nullptr,        parse_binary,    precedence::PREC_AND}},
    {token_type::bar_bar,             {nullptr,        parse_binary,    precedence::PREC_OR}},
    {token_type::identifier,          {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_i32,              {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_i64,              {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_f64,              {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_u64,              {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_char,             {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_bool,             {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_null,             {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_nullptr,          {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_arena,            {parse_name,     nullptr,         precedence::PREC_NONE}},
    {token_type::kw_typeof,           {parse_typeof,   nullptr,         precedence::PREC_NONE}},
    {token_type::kw_sizeof,           {parse_sizeof,   nullptr,         precedence::PREC_NONE}},
    {token_type::left_bracket,        {parse_array,    parse_subscript, precedence::PREC_CALL}},
    {token_type::dot,                 {nullptr,        parse_dot,       precedence::PREC_CALL}},
    {token_type::kw_const,            {nullptr,        parse_const,     precedence::PREC_CALL}},
    {token_type::at,                  {nullptr,        parse_at,        precedence::PREC_CALL}},
    {token_type::ampersand,           {nullptr,        parse_ampersand, precedence::PREC_CALL}},
    {token_type::kw_function,         {parse_func_ptr, nullptr,         precedence::PREC_NONE}}
};
static constexpr auto default_rule = parse_rule{nullptr, nullptr, precedence::PREC_NONE};

auto get_rule(token_type tt) -> const parse_rule*
{
    if (rules.contains(tt)) return &rules.at(tt);
    return &default_rule;
}

}

auto parse_expression(tokenstream& tokens) -> node_expr_ptr
{
    return parse_precedence(tokens, precedence::PREC_OR);
}
    
}