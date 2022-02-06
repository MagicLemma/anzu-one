#include "runtime.hpp"
#include "utility/print.hpp"

#include <utility>

namespace anzu {
namespace {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

template <typename... Args>
void verify(bool condition, std::string_view msg, Args&&... args)
{
    if (!condition) {
        anzu::print(msg, std::forward<Args>(args)...);
        std::exit(1);
    }
}

void verify_stack(const runtime_context& ctx, int count, std::string_view name)
{
    const auto type = count != 1 ? "args" : "arg";
    const auto err = "stack underflow: '{}' requires {} {}";
    anzu::verify(ctx.size() >= count, err, name, count, type);
}

}

auto memory::insert(const std::string& name, const anzu::object& value) -> anzu::object&
{
    auto [iter, success] = d_values.insert_or_assign(name, value);
    return iter->second;
}

auto memory::get(const std::string& name) -> anzu::object&
{
    if (!d_values.contains(name)) {
        anzu::print("Error: Unknown value '{}'", name);
        std::exit(1);
    }
    return d_values.at(name);
}

auto memory::print() const -> void
{
    anzu::print("Values:\n");
    for (const auto& [key, val] : d_values) {
        anzu::print(" - {} -> {}\n", key, val);
    }
}

auto runtime_context::push_frame() -> frame&
{
    d_frames.push_back({});
    return d_frames.back();
}

auto runtime_context::pop_frame() -> void
{
    d_frames.pop_back();
}

auto runtime_context::peek_frame(std::size_t index) -> frame&
{
    return d_frames[d_frames.size() - index - 1];
}

auto runtime_context::push_value(const object& val) -> object&
{
    d_values.push_back(val);
    return d_values.back();
}

auto runtime_context::pop_value() -> object
{
    auto ret = d_values.back();
    d_values.pop_back();
    return ret;
}

auto runtime_context::peek_value(std::size_t index) -> object&
{
    return d_values[d_values.size() - index - 1];
}

auto runtime_context::size() const -> std::size_t
{
    return d_values.size();
}

auto apply_op(runtime_context& ctx, const op& op_code) -> void
{
    std::visit(overloaded {
        [&](const op_push_const& op) {
            ctx.push_value(op.value);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_push_var& op) {
            auto& frame = ctx.peek_frame();
            ctx.push_value(frame.memory.get(op.name));
            frame.ptr += 1;
        },
        [&](const op_pop& op) {
            verify_stack(ctx, 1, "pop");
            ctx.pop_value();
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_copy_index& op) {
            verify_stack(ctx, op.index + 1, "copy_index");
            ctx.push_value(ctx.peek_value(op.index));
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_store& op) {
            verify_stack(ctx, 1, "store");
            auto& frame = ctx.peek_frame();
            frame.memory.insert(op.name, ctx.pop_value());
            frame.ptr += 1;
        },
        [&](const op_if& op) {
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_if_end& op) {
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_else& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_while& op) {
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_while_end& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_for& op) {
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_for_end& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_break& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_continue& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_jump_if_false& op) {
            if (ctx.pop_value().to_bool()) {
                ctx.peek_frame().ptr += 1;
            } else {
                ctx.peek_frame().ptr = op.jump;
            }
        },
        [&](const op_function& op) {
            ctx.peek_frame().ptr = op.jump;
        },
        [&](const op_function_end& op) {
            ctx.push_value(anzu::null_object());
            ctx.pop_frame();
        },
        [&](const op_return& op) {
            ctx.pop_frame();
        },
        [&](const op_function_call& op) {
            ctx.peek_frame().ptr += 1; // Position after function call

            auto& frame = ctx.push_frame(); 
            frame.ptr = op.ptr; // Jump into the function

            // Pop elements off the stack and load them into the new scope
            for (const auto& arg : op.sig.args | std::views::reverse) {
                frame.memory.insert(arg.name, ctx.pop_value());
            }
        },
        [&](const op_builtin_call& op) {
            op.ptr(ctx);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_add& op) {
            verify_stack(ctx, 2, "+");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a + b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_sub& op) {
            verify_stack(ctx, 2, "-");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a - b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_mul& op) {
            verify_stack(ctx, 2, "*");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a * b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_div& op) {
            verify_stack(ctx, 2, "/");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a / b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_mod& op) {
            verify_stack(ctx, 2, "%");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a % b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_eq& op) {
            verify_stack(ctx, 2, "==");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a == b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_ne& op) {
            verify_stack(ctx, 2, "!=");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a != b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_lt& op) {
            verify_stack(ctx, 2, "<");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a < b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_le& op) {
            verify_stack(ctx, 2, "<=");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a <= b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_gt& op) {
            verify_stack(ctx, 2, ">");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a > b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_ge& op) {
            verify_stack(ctx, 2, ">=");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a >= b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_or& op) {
            verify_stack(ctx, 2, "or");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a || b);
            ctx.peek_frame().ptr += 1;
        },
        [&](const op_and& op) {
            verify_stack(ctx, 2, "and");
            auto b = ctx.pop_value();
            auto a = ctx.pop_value();
            ctx.push_value(a && b);
            ctx.peek_frame().ptr += 1;
        }
    }, op_code);
}

auto run_program(const anzu::program& program) -> void
{
    runtime_context ctx;
    ctx.push_frame();

    while (ctx.peek_frame().ptr < std::ssize(program)) {
        apply_op(ctx, program[ctx.peek_frame().ptr]);
    }
}

auto run_program_debug(const anzu::program& program) -> void
{
    anzu::runtime_context ctx;
    ctx.push_frame();

    while (ctx.peek_frame().ptr < std::ssize(program)) {
        const auto& op = program[ctx.peek_frame().ptr];
        anzu::print("{:>4} - {}\n", ctx.peek_frame().ptr, anzu::to_string(op));
        apply_op(ctx, program[ctx.peek_frame().ptr]);
    }
}

}