#pragma once

#if defined(FORGE_CONTRACT_GUEST)

#include <string_view>

#include <forge/contract/intrinsics.hpp>

#define FORGE_POLICY_THROW_EXCEPTION(ExceptionType, Message, ...)                                                      \
   do {                                                                                                                \
      auto&& forge_exception_policy_storage = (Message);                                                               \
      const auto forge_exception_policy_message = std::string_view{forge_exception_policy_storage};                    \
      ::forge::contract::intrinsic::eosio_assert_message(                                                              \
          0U, forge_exception_policy_message.data(),                                                                   \
          static_cast<std::uint32_t>(forge_exception_policy_message.size()));                                          \
      __builtin_unreachable();                                                                                         \
   } while (false)

#define FORGE_POLICY_THROW_STANDARD(ExceptionType, Message)                                                            \
   do {                                                                                                                \
      auto&& forge_exception_policy_storage = (Message);                                                               \
      const auto forge_exception_policy_message = std::string_view{forge_exception_policy_storage};                    \
      ::forge::contract::intrinsic::eosio_assert_message(                                                              \
          0U, forge_exception_policy_message.data(),                                                                   \
          static_cast<std::uint32_t>(forge_exception_policy_message.size()));                                          \
      __builtin_unreachable();                                                                                         \
   } while (false)

#else

#include <string>

#include <forge/exceptions/macros.hpp>

#define FORGE_POLICY_THROW_EXCEPTION(ExceptionType, Message, ...)                                                      \
   FORGE_THROW_EXCEPTION(ExceptionType, Message __VA_OPT__(, ) __VA_ARGS__)

#define FORGE_POLICY_THROW_STANDARD(ExceptionType, Message) throw ExceptionType(std::string(Message))

#endif
