#!/usr/bin/env python3
"""Prepare the pinned EOS VM source for its original Catch2 oracle suite."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import subprocess


DONOR_COMMIT = "e5b1fc79c4b8d78f32749afa94a8d4c4d071f67f"
SIGNALS_SHA256 = "38b891f1614d110b662f83d251a46507dd27820c58c8e9676fbe1bcce9246672"
HOST_FUNCTION_SHA256 = "1e50e42446613e39c2a0f45275e2e35b48be891e0573ebfa2f718bd83b5b1eb3"


def git_output(root: pathlib.Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True
    ).strip()


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError("pinned donor signal-handler patch no longer applies exactly once")
    return text.replace(old, new)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--donor", required=True, type=pathlib.Path)
    args = parser.parse_args()

    donor = args.donor.resolve()
    if git_output(donor, "rev-parse", "HEAD") != DONOR_COMMIT:
        raise RuntimeError(f"donor HEAD must be {DONOR_COMMIT}")

    signals = donor / "include/eosio/vm/signals.hpp"
    if sha256(signals) != SIGNALS_SHA256:
        raise RuntimeError("pinned donor signals.hpp hash does not match the audited source")

    text = signals.read_text()
    text = replace_once(
        text,
        "#include <eosio/vm/exceptions.hpp>\n",
        "#include <eosio/vm/allocator.hpp>\n#include <eosio/vm/exceptions.hpp>\n",
    )
    text = replace_once(
        text,
        "[[gnu::noinline]] auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator& code_allocator, wasm_allocator* mem_allocator) {",
        "[[gnu::noinline]] auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator* code_allocator, wasm_allocator* mem_allocator) {",
    )
    text = replace_once(
        text,
        "      code_memory_range = code_allocator.get_code_span();\n"
        "      memory_range = mem_allocator->get_span();",
        "      code_memory_range = code_allocator ? code_allocator->get_code_span() : std::span<std::byte>{};\n"
        "      memory_range = mem_allocator ? mem_allocator->get_span() : std::span<std::byte>{};",
    )
    text = replace_once(
        text,
        "\n}} // namespace eosio::vm\n",
        "\n"
        "   template<typename F, typename E>\n"
        "   auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator& code_allocator, wasm_allocator* mem_allocator) {\n"
        "      return invoke_with_signal_handler(std::forward<F>(f), std::forward<E>(e), &code_allocator, mem_allocator);\n"
        "   }\n"
        "\n"
        "}} // namespace eosio::vm\n",
    )
    signals.write_text(text)

    host_function = donor / "include/eosio/vm/host_function.hpp"
    if sha256(host_function) != HOST_FUNCTION_SHA256:
        raise RuntimeError("pinned donor host_function.hpp hash does not match the audited source")

    text = host_function.read_text()
    text = replace_once(
        text,
        """      template <typename T>
      inline constexpr auto as_result(T&& val) const {
         if constexpr (std::is_integral_v<T> && sizeof(T) == 4)
            return i32_const_t{ static_cast<uint32_t>(val) };
         else if constexpr (std::is_integral_v<T> && sizeof(T) == 8)
            return i64_const_t{ static_cast<uint64_t>(val) };
         else if constexpr (std::is_floating_point_v<T> && sizeof(T) == 4)
            return f32_const_t{ static_cast<float>(val) };
         else if constexpr (std::is_floating_point_v<T> && sizeof(T) == 8)
            return f64_const_t{ static_cast<double>(val) };
         else if constexpr (std::is_void_v<std::decay_t<std::remove_pointer_t<T>>>)
            return i32_const_t{ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(val) -
                                                      reinterpret_cast<uintptr_t>(this->access())) };
         else
            return no_match_t{};
      }
""",
        """      template <typename T>
      inline constexpr auto as_result(T&& val) const {
         using value_type = std::remove_cvref_t<T>;
         if constexpr (std::is_integral_v<value_type> && sizeof(value_type) == 4)
            return i32_const_t{ static_cast<uint32_t>(val) };
         else if constexpr (std::is_integral_v<value_type> && sizeof(value_type) == 8)
            return i64_const_t{ static_cast<uint64_t>(val) };
         else if constexpr (std::is_floating_point_v<value_type> && sizeof(value_type) == 4)
            return f32_const_t{ static_cast<float>(val) };
         else if constexpr (std::is_floating_point_v<value_type> && sizeof(value_type) == 8)
            return f64_const_t{ static_cast<double>(val) };
         else if constexpr (std::is_void_v<std::remove_cv_t<std::remove_pointer_t<value_type>>>)
            return i32_const_t{ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(val) -
                                                      reinterpret_cast<uintptr_t>(this->access())) };
         else
            return no_match_t{};
      }
""",
    )
    text = replace_once(
        text,
        """      template <typename Type_Converter, typename T>
      constexpr auto resolve_result(Type_Converter& tc, T&& val) {
         if constexpr (has_to_wasm_v<T, Type_Converter>) {
            return tc.as_result(tc.to_wasm(std::forward<T>(val)));
         } else {
            return tc.as_result(std::forward<T>(val));
         }
      }
""",
        """      template <typename Type_Converter, typename T>
      constexpr auto resolve_result(Type_Converter& tc, T&& val) {
         using direct_result = decltype(tc.as_result(std::forward<T>(val)));
         if constexpr (!std::is_same_v<std::decay_t<direct_result>, no_match_t>) {
            return tc.as_result(std::forward<T>(val));
         } else if constexpr (std::is_pointer_v<std::remove_cvref_t<T>> &&
                              has_to_wasm_v<std::remove_cvref_t<T>, Type_Converter>) {
            return tc.as_result(tc.to_wasm(std::remove_cvref_t<T>{val}));
         } else if constexpr (has_to_wasm_v<T, Type_Converter>) {
            return tc.as_result(tc.to_wasm(std::forward<T>(val)));
         } else {
            return tc.as_result(std::forward<T>(val));
         }
      }
""",
    )
    host_function.write_text(text)

    changed_tests = git_output(donor, "diff", "--name-only", "--", "tests")
    if changed_tests:
        raise RuntimeError(f"donor test bodies were modified:\n{changed_tests}")

    changed = git_output(donor, "diff", "--name-only")
    if changed != "include/eosio/vm/host_function.hpp\ninclude/eosio/vm/signals.hpp":
        raise RuntimeError(f"unexpected donor oracle changes:\n{changed}")

    print("prepared pinned donor oracle; original Catch2 test bodies remain unchanged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
