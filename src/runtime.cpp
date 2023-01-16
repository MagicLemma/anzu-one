#include "runtime.hpp"
#include "object.hpp"
#include "utility/print.hpp"
#include "utility/overloaded.hpp"
#include "utility/scope_timer.hpp"
#include "utility/memory.hpp"

#include <chrono>
#include <algorithm>
#include <functional>
#include <utility>

namespace anzu {
namespace {

constexpr auto top_bit = (std::numeric_limits<std::uint64_t>::max() / 2) + 1;

auto set_top_bit(std::uint64_t x) -> std::uint64_t
{
    constexpr auto top_bit = std::uint64_t{1} << 63;
    return x | top_bit; 
}

auto unset_top_bit(std::uint64_t x) -> std::uint64_t
{
    constexpr auto top_bit = std::uint64_t{1} << 63;
    return x & ~top_bit; 
}

auto get_top_bit(std::uint64_t x) -> bool
{
    constexpr auto top_bit = std::uint64_t{1} << 63;
    return x & top_bit;
}

}

template <typename ...Args>
auto runtime_assert(bool condition, std::string_view msg, Args&&... args)
{
    if (!condition) {
        anzu::print(msg, std::forward<Args>(args)...);
        std::exit(1);
    }
}

template <typename Type, template <typename> typename Op>
auto binary_op(runtime_context& ctx) -> void
{
    static constexpr auto op = Op<Type>{};
    const auto rhs = pop_value<Type>(ctx.stack);
    const auto lhs = pop_value<Type>(ctx.stack);
    push_value(ctx.stack, op(lhs, rhs));
    ++ctx.prog_ptr;
}

auto apply_op(runtime_context& ctx, const op& op_code) -> void
{
    std::visit(overloaded {
        [&](const op_load_bytes& op) {
            for (const auto byte : op.bytes) {
                ctx.stack.push_back(byte);
            }
            ++ctx.prog_ptr;
        },
        [&](op_push_global_addr op) {
            push_value(ctx.stack, op.position);
            ++ctx.prog_ptr;
        },
        [&](op_push_local_addr op) {
            push_value(ctx.stack, ctx.base_ptr + op.offset);
            ++ctx.prog_ptr;
        },

        [&](op_i32_add) { binary_op<std::int32_t, std::plus>(ctx); },
        [&](op_i32_sub) { binary_op<std::int32_t, std::minus>(ctx); },
        [&](op_i32_mul) { binary_op<std::int32_t, std::multiplies>(ctx); },
        [&](op_i32_div) { binary_op<std::int32_t, std::divides>(ctx); },
        [&](op_i32_eq)  { binary_op<std::int32_t, std::equal_to>(ctx); },
        [&](op_i32_ne)  { binary_op<std::int32_t, std::not_equal_to>(ctx); },
        [&](op_i32_lt)  { binary_op<std::int32_t, std::less>(ctx); },
        [&](op_i32_le)  { binary_op<std::int32_t, std::less_equal>(ctx); },
        [&](op_i32_gt)  { binary_op<std::int32_t, std::greater>(ctx); },
        [&](op_i32_ge)  { binary_op<std::int32_t, std::greater_equal>(ctx); },

        [&](op_i64_add) { binary_op<std::int64_t, std::plus>(ctx); },
        [&](op_i64_sub) { binary_op<std::int64_t, std::minus>(ctx); },
        [&](op_i64_mul) { binary_op<std::int64_t, std::multiplies>(ctx); },
        [&](op_i64_div) { binary_op<std::int64_t, std::divides>(ctx); },
        [&](op_i64_eq)  { binary_op<std::int64_t, std::equal_to>(ctx); },
        [&](op_i64_ne)  { binary_op<std::int64_t, std::not_equal_to>(ctx); },
        [&](op_i64_lt)  { binary_op<std::int64_t, std::less>(ctx); },
        [&](op_i64_le)  { binary_op<std::int64_t, std::less_equal>(ctx); },
        [&](op_i64_gt)  { binary_op<std::int64_t, std::greater>(ctx); },
        [&](op_i64_ge)  { binary_op<std::int64_t, std::greater_equal>(ctx); },

        [&](op_u64_add) { binary_op<std::uint64_t, std::plus>(ctx); },
        [&](op_u64_sub) { binary_op<std::uint64_t, std::minus>(ctx); },
        [&](op_u64_mul) { binary_op<std::uint64_t, std::multiplies>(ctx); },
        [&](op_u64_div) { binary_op<std::uint64_t, std::divides>(ctx); },
        [&](op_u64_eq)  { binary_op<std::uint64_t, std::equal_to>(ctx); },
        [&](op_u64_ne)  { binary_op<std::uint64_t, std::not_equal_to>(ctx); },
        [&](op_u64_lt)  { binary_op<std::uint64_t, std::less>(ctx); },
        [&](op_u64_le)  { binary_op<std::uint64_t, std::less_equal>(ctx); },
        [&](op_u64_gt)  { binary_op<std::uint64_t, std::greater>(ctx); },
        [&](op_u64_ge)  { binary_op<std::uint64_t, std::greater_equal>(ctx); },

        [&](op_f64_add) { binary_op<double, std::plus>(ctx); },
        [&](op_f64_sub) { binary_op<double, std::minus>(ctx); },
        [&](op_f64_mul) { binary_op<double, std::multiplies>(ctx); },
        [&](op_f64_div) { binary_op<double, std::divides>(ctx); },
        [&](op_f64_eq)  { binary_op<double, std::equal_to>(ctx); },
        [&](op_f64_ne)  { binary_op<double, std::not_equal_to>(ctx); },
        [&](op_f64_lt)  { binary_op<double, std::less>(ctx); },
        [&](op_f64_le)  { binary_op<double, std::less_equal>(ctx); },
        [&](op_f64_gt)  { binary_op<double, std::greater>(ctx); },
        [&](op_f64_ge)  { binary_op<double, std::greater_equal>(ctx); },

        [&](op_bool_and) { binary_op<bool, std::logical_and>(ctx); },
        [&](op_bool_or)  { binary_op<bool, std::logical_or>(ctx); },
        [&](op_bool_eq)  { binary_op<bool, std::equal_to>(ctx); },
        [&](op_bool_ne)  { binary_op<bool, std::not_equal_to>(ctx); },

        [&](op_load op) {
            const auto ptr = pop_value<std::uint64_t>(ctx.stack);
            
            if (get_top_bit(ptr)) {
                const auto heap_ptr = unset_top_bit(ptr);
                for (std::size_t i = 0; i != op.size; ++i) {
                    ctx.stack.push_back(ctx.heap[heap_ptr + i]);
                }
            } else {
                for (std::size_t i = 0; i != op.size; ++i) {
                    ctx.stack.push_back(ctx.stack[ptr + i]);
                }
            }

            ++ctx.prog_ptr;
        },
        [&](op_save op) {
            const auto ptr = pop_value<std::uint64_t>(ctx.stack);

            if (get_top_bit(ptr)) {
                const auto heap_ptr = unset_top_bit(ptr);
                //runtime_assert(ptr + op.size <= ctx.stack.size(), "tried to access invalid memory address {}", ptr);
                std::memcpy(&ctx.heap[heap_ptr], &ctx.stack[ctx.stack.size() - op.size], op.size);
                ctx.stack.resize(ctx.stack.size() - op.size);
            } else {
                runtime_assert(ptr + op.size <= ctx.stack.size(), "tried to access invalid memory address {}", ptr);
                if (ptr + op.size < ctx.stack.size()) {
                    std::memcpy(&ctx.stack[ptr], &ctx.stack[ctx.stack.size() - op.size], op.size);
                    ctx.stack.resize(ctx.stack.size() - op.size);
                }
            }

            ++ctx.prog_ptr;
        },
        [&](op_pop op) {
            ctx.stack.resize(ctx.stack.size() - op.size);
            ++ctx.prog_ptr;
        },
        [&](op_allocate op) {
            const auto count = pop_value<std::uint64_t>(ctx.stack);
            const auto ptr = ctx.allocator.allocate(count * op.type_size + sizeof(std::uint64_t));
            write_value(ctx.heap, ptr, count * op.type_size); // Store the size at the pointer
            push_value(ctx.stack, set_top_bit(ptr + sizeof(std::uint64_t))); // Return pointer past the size
            ++ctx.prog_ptr;
        },
        [&](op_deallocate) {
            const auto ptr = pop_value<std::uint64_t>(ctx.stack);
            runtime_assert(get_top_bit(ptr), "cannot delete a pointer to stack memory\n");
            const auto heap_ptr = unset_top_bit(ptr) - sizeof(std::uint64_t);
            const auto size = read_value<std::uint64_t>(ctx.heap, heap_ptr);
            ctx.allocator.deallocate(heap_ptr, size + sizeof(std::uint64_t));
            ++ctx.prog_ptr;
        },
        [&](op_jump op) {
            ctx.prog_ptr += op.jump;
        },
        [&](op_jump_if_false op) {
            if (pop_value<bool>(ctx.stack)) {
                ++ctx.prog_ptr;
            } else {
                ctx.prog_ptr += op.jump;
            }
        },
        [&](const op_function& op) {
            ctx.prog_ptr = op.jump;
        },
        [&](op_return op) {
            const auto prev_base_ptr = read_value<std::uint64_t>(ctx.stack, ctx.base_ptr);
            const auto prev_prog_ptr = read_value<std::uint64_t>(ctx.stack, ctx.base_ptr + sizeof(std::uint64_t));
            
            std::memcpy(&ctx.stack[ctx.base_ptr], &ctx.stack[ctx.stack.size() - op.size], op.size);
            ctx.stack.resize(ctx.base_ptr + op.size);
            ctx.base_ptr = prev_base_ptr;
            ctx.prog_ptr = prev_prog_ptr;
        },
        [&](const op_function_call& op) {
            // Store the old base_ptr and prog_ptr so that they can be restored at the end of
            // the function.
            const auto new_base_ptr = ctx.stack.size() - op.args_size;
            write_value(ctx.stack, new_base_ptr, ctx.base_ptr);
            write_value(ctx.stack, new_base_ptr + sizeof(std::uint64_t), ctx.prog_ptr + 1); // Pos after function call
            
            ctx.base_ptr = new_base_ptr;
            ctx.prog_ptr = op.ptr; // Jump into the function
        },
        [&](const op_builtin_call& op) {
            op.ptr(ctx.stack);
            ++ctx.prog_ptr;
        },
        [&](const op_debug& op) {
            print(op.message);
            ++ctx.prog_ptr;
        }
    }, op_code);
}

auto run_program(const anzu::program& program) -> void
{
    const auto timer = scope_timer{};

    runtime_context ctx;
    while (ctx.prog_ptr < program.size()) {
        apply_op(ctx, program[ctx.prog_ptr]);
    }

    if (ctx.allocator.bytes_allocated() > 0) {
        anzu::print("\n -> Heap Size: {}, fix your memory leak!\n", ctx.allocator.bytes_allocated());
    }
}

auto run_program_debug(const anzu::program& program) -> void
{
    const auto timer = scope_timer{};

    runtime_context ctx;
    while (ctx.prog_ptr < program.size()) {
        const auto& op = program[ctx.prog_ptr];
        anzu::print("{:>4} - {}\n", ctx.prog_ptr, op);
        apply_op(ctx, program[ctx.prog_ptr]);
        anzu::print("Stack: {}\n", format_comma_separated(ctx.stack));
        anzu::print("Heap: allocated={}\n", ctx.allocator.bytes_allocated());
    }

    if (ctx.allocator.bytes_allocated() > 0) {
        anzu::print("\n -> Heap Size: {}, fix your memory leak!\n", ctx.allocator.bytes_allocated());
    }
}

}