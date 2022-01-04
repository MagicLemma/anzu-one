#pragma once
#include "stack_frame.hpp"

#include <fmt/format.h>
#include <variant>

namespace anzu {
namespace op {

struct dump
{
    void print() const { fmt::print("OP_DUMP\n"); }
    void apply(anzu::stack_frame& frame) const;
};

struct pop
{
    void print() const { fmt::print("OP_POP\n"); }
    void apply(anzu::stack_frame& frame) const;
};

struct push_int
{
    int value;

    void print() const { fmt::print("OP_PUSH_INT({})\n", value); }
    void apply(anzu::stack_frame& frame) const;
};

struct store_int
{
    std::string name;
    int value;

    void print() const { fmt::print("OP_STORE_INT({}, {})\n", name, value); }
    void apply(anzu::stack_frame& frame) const;
};

struct push_var
{
    std::string name;

    void print() const { fmt::print("OP_PUSH_VAR({})\n", name); }
    void apply(anzu::stack_frame& frame) const;
};

struct store_var
{
    std::string name;
    std::string source; 

    void print() const { fmt::print("OP_STORE_VAR({}, {})\n", name, source); }
    void apply(anzu::stack_frame& frame) const;
};

struct add
{
    void print() const { fmt::print("OP_ADD\n"); }
    void apply(anzu::stack_frame& frame) const;
};

struct sub
{
    void print() const { fmt::print("OP_SUB\n"); }
    void apply(anzu::stack_frame& frame) const;
};

struct dup
{
    void print() const { fmt::print("OP_DUP\n"); }
    void apply(anzu::stack_frame& frame) const;
};

struct print_frame
{
    void print() const { fmt::print("OP_PRINT_FRAME\n"); }
    void apply(anzu::stack_frame& frame) const;
};

}

using opcode = std::variant<
    op::dump,
    op::pop,
    op::push_int,
    op::store_int,
    op::push_var,
    op::store_var,
    op::add,
    op::sub,
    op::dup,
    op::print_frame
>;

}