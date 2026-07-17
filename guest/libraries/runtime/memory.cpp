#include <cstddef>
#include "details/memory.hxx"

#include <cstdint>
#include <errno.h>

namespace {

bool contains(const char* value, char character) {
   while (*value != '\0') {
      if (*value == character) {
         return true;
      }
      ++value;
   }
   return false;
}

char* error_name(int error) {
#define FORGE_CONTRACT_ERRNO_CASE(value)                                                                               \
   case value:                                                                                                         \
      return const_cast<char*>(#value)

   switch (error) {
   case 0:
      return const_cast<char*>("No error");
      FORGE_CONTRACT_ERRNO_CASE(EPERM);
      FORGE_CONTRACT_ERRNO_CASE(ENOENT);
      FORGE_CONTRACT_ERRNO_CASE(ESRCH);
      FORGE_CONTRACT_ERRNO_CASE(EINTR);
      FORGE_CONTRACT_ERRNO_CASE(EIO);
      FORGE_CONTRACT_ERRNO_CASE(ENXIO);
      FORGE_CONTRACT_ERRNO_CASE(E2BIG);
      FORGE_CONTRACT_ERRNO_CASE(ENOEXEC);
      FORGE_CONTRACT_ERRNO_CASE(EBADF);
      FORGE_CONTRACT_ERRNO_CASE(ECHILD);
      FORGE_CONTRACT_ERRNO_CASE(EAGAIN);
      FORGE_CONTRACT_ERRNO_CASE(ENOMEM);
      FORGE_CONTRACT_ERRNO_CASE(EACCES);
      FORGE_CONTRACT_ERRNO_CASE(EFAULT);
      FORGE_CONTRACT_ERRNO_CASE(EBUSY);
      FORGE_CONTRACT_ERRNO_CASE(EEXIST);
      FORGE_CONTRACT_ERRNO_CASE(EXDEV);
      FORGE_CONTRACT_ERRNO_CASE(ENODEV);
      FORGE_CONTRACT_ERRNO_CASE(ENOTDIR);
      FORGE_CONTRACT_ERRNO_CASE(EISDIR);
      FORGE_CONTRACT_ERRNO_CASE(EINVAL);
      FORGE_CONTRACT_ERRNO_CASE(ENFILE);
      FORGE_CONTRACT_ERRNO_CASE(EMFILE);
      FORGE_CONTRACT_ERRNO_CASE(ENOTTY);
      FORGE_CONTRACT_ERRNO_CASE(EFBIG);
      FORGE_CONTRACT_ERRNO_CASE(ENOSPC);
      FORGE_CONTRACT_ERRNO_CASE(ESPIPE);
      FORGE_CONTRACT_ERRNO_CASE(EROFS);
      FORGE_CONTRACT_ERRNO_CASE(EMLINK);
      FORGE_CONTRACT_ERRNO_CASE(EPIPE);
      FORGE_CONTRACT_ERRNO_CASE(EDOM);
      FORGE_CONTRACT_ERRNO_CASE(ERANGE);
      FORGE_CONTRACT_ERRNO_CASE(EDEADLK);
      FORGE_CONTRACT_ERRNO_CASE(ENAMETOOLONG);
      FORGE_CONTRACT_ERRNO_CASE(ENOLCK);
      FORGE_CONTRACT_ERRNO_CASE(ENOSYS);
      FORGE_CONTRACT_ERRNO_CASE(ENOTEMPTY);
      FORGE_CONTRACT_ERRNO_CASE(ELOOP);
      FORGE_CONTRACT_ERRNO_CASE(ENOMSG);
      FORGE_CONTRACT_ERRNO_CASE(EIDRM);
      FORGE_CONTRACT_ERRNO_CASE(ENOSTR);
      FORGE_CONTRACT_ERRNO_CASE(ENODATA);
      FORGE_CONTRACT_ERRNO_CASE(ETIME);
      FORGE_CONTRACT_ERRNO_CASE(ENOSR);
      FORGE_CONTRACT_ERRNO_CASE(ENOLINK);
      FORGE_CONTRACT_ERRNO_CASE(EPROTO);
      FORGE_CONTRACT_ERRNO_CASE(EBADMSG);
      FORGE_CONTRACT_ERRNO_CASE(EOVERFLOW);
      FORGE_CONTRACT_ERRNO_CASE(EILSEQ);
      FORGE_CONTRACT_ERRNO_CASE(ENOTSOCK);
      FORGE_CONTRACT_ERRNO_CASE(EDESTADDRREQ);
      FORGE_CONTRACT_ERRNO_CASE(EMSGSIZE);
      FORGE_CONTRACT_ERRNO_CASE(EPROTOTYPE);
      FORGE_CONTRACT_ERRNO_CASE(ENOPROTOOPT);
      FORGE_CONTRACT_ERRNO_CASE(EPROTONOSUPPORT);
      FORGE_CONTRACT_ERRNO_CASE(EAFNOSUPPORT);
      FORGE_CONTRACT_ERRNO_CASE(EADDRINUSE);
      FORGE_CONTRACT_ERRNO_CASE(EADDRNOTAVAIL);
      FORGE_CONTRACT_ERRNO_CASE(ENETDOWN);
      FORGE_CONTRACT_ERRNO_CASE(ENETUNREACH);
      FORGE_CONTRACT_ERRNO_CASE(ENETRESET);
      FORGE_CONTRACT_ERRNO_CASE(ECONNABORTED);
      FORGE_CONTRACT_ERRNO_CASE(ECONNRESET);
      FORGE_CONTRACT_ERRNO_CASE(ENOBUFS);
      FORGE_CONTRACT_ERRNO_CASE(EISCONN);
      FORGE_CONTRACT_ERRNO_CASE(ENOTCONN);
      FORGE_CONTRACT_ERRNO_CASE(ETIMEDOUT);
      FORGE_CONTRACT_ERRNO_CASE(ECONNREFUSED);
      FORGE_CONTRACT_ERRNO_CASE(EHOSTUNREACH);
      FORGE_CONTRACT_ERRNO_CASE(EALREADY);
      FORGE_CONTRACT_ERRNO_CASE(EINPROGRESS);
      FORGE_CONTRACT_ERRNO_CASE(ECANCELED);
      FORGE_CONTRACT_ERRNO_CASE(EOWNERDEAD);
      FORGE_CONTRACT_ERRNO_CASE(ENOTRECOVERABLE);
      FORGE_CONTRACT_ERRNO_CASE(ENOTSUP);
   default:
      return const_cast<char*>("Unknown error");
   }

#undef FORGE_CONTRACT_ERRNO_CASE
}

} // namespace

extern "C" {

void* memset(void* destination, int value, std::size_t size) {
   auto* output = static_cast<std::uint8_t*>(destination);
   for (auto index = std::size_t{0}; index < size; ++index) {
      output[index] = static_cast<std::uint8_t>(value);
   }
   return destination;
}

void* memcpy(void* destination, const void* source, std::size_t size) {
   auto* output = static_cast<std::uint8_t*>(destination);
   const auto* input = static_cast<const std::uint8_t*>(source);
   for (auto index = std::size_t{0}; index < size; ++index) {
      output[index] = input[index];
   }
   return destination;
}

void* memmove(void* destination, const void* source, std::size_t size) {
   auto* output = static_cast<std::uint8_t*>(destination);
   const auto* input = static_cast<const std::uint8_t*>(source);
   const auto output_address = reinterpret_cast<std::uintptr_t>(output);
   const auto input_address = reinterpret_cast<std::uintptr_t>(input);
   if (output_address < input_address) {
      for (auto index = std::size_t{0}; index < size; ++index) {
         output[index] = input[index];
      }
   } else if (output_address > input_address) {
      for (auto index = size; index != 0U; --index) {
         output[index - 1U] = input[index - 1U];
      }
   }
   return destination;
}

int memcmp(const void* left, const void* right, std::size_t size) {
   const auto* lhs = static_cast<const std::uint8_t*>(left);
   const auto* rhs = static_cast<const std::uint8_t*>(right);
   for (auto index = std::size_t{0}; index < size; ++index) {
      if (lhs[index] != rhs[index]) {
         return lhs[index] < rhs[index] ? -1 : 1;
      }
   }
   return 0;
}

void* memchr(const void* source, int value, std::size_t size) {
   const auto* bytes = static_cast<const std::uint8_t*>(source);
   const auto expected = static_cast<std::uint8_t>(value);
   for (auto index = std::size_t{0}; index < size; ++index) {
      if (bytes[index] == expected) {
         return const_cast<std::uint8_t*>(bytes + index);
      }
   }
   return nullptr;
}

std::size_t strlen(const char* value) {
   auto size = std::size_t{0};
   while (value[size] != '\0') {
      ++size;
   }
   return size;
}

int strcmp(const char* left, const char* right) {
   while (*left != '\0' && *left == *right) {
      ++left;
      ++right;
   }
   return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
}

int strncmp(const char* left, const char* right, std::size_t size) {
   for (auto index = std::size_t{0}; index < size; ++index) {
      const auto lhs = static_cast<unsigned char>(left[index]);
      const auto rhs = static_cast<unsigned char>(right[index]);
      if (lhs != rhs || lhs == 0U) {
         return static_cast<int>(lhs) - static_cast<int>(rhs);
      }
   }
   return 0;
}

char* strcpy(char* destination, const char* source) {
   auto index = std::size_t{0};
   do {
      destination[index] = source[index];
   } while (source[index++] != '\0');
   return destination;
}

char* strncpy(char* destination, const char* source, std::size_t size) {
   auto index = std::size_t{0};
   while (index < size && source[index] != '\0') {
      destination[index] = source[index];
      ++index;
   }
   while (index < size) {
      destination[index++] = '\0';
   }
   return destination;
}

char* strcat(char* destination, const char* source) {
   strcpy(destination + strlen(destination), source);
   return destination;
}

char* strncat(char* destination, const char* source, std::size_t size) {
   auto* output = destination + strlen(destination);
   auto index = std::size_t{0};
   while (index < size && source[index] != '\0') {
      output[index] = source[index];
      ++index;
   }
   output[index] = '\0';
   return destination;
}

char* strchr(const char* value, int character) {
   const auto expected = static_cast<unsigned char>(character);
   do {
      if (static_cast<unsigned char>(*value) == expected) {
         return const_cast<char*>(value);
      }
   } while (*value++ != '\0');
   return nullptr;
}

char* strrchr(const char* value, int character) {
   const auto expected = static_cast<unsigned char>(character);
   const char* result = nullptr;
   do {
      if (static_cast<unsigned char>(*value) == expected) {
         result = value;
      }
   } while (*value++ != '\0');
   return const_cast<char*>(result);
}

std::size_t strspn(const char* value, const char* accepted) {
   auto size = std::size_t{0};
   while (value[size] != '\0' && contains(accepted, value[size])) {
      ++size;
   }
   return size;
}

std::size_t strcspn(const char* value, const char* rejected) {
   auto size = std::size_t{0};
   while (value[size] != '\0' && !contains(rejected, value[size])) {
      ++size;
   }
   return size;
}

char* strpbrk(const char* value, const char* accepted) {
   while (*value != '\0') {
      if (contains(accepted, *value)) {
         return const_cast<char*>(value);
      }
      ++value;
   }
   return nullptr;
}

char* strstr(const char* value, const char* substring) {
   if (*substring == '\0') {
      return const_cast<char*>(value);
   }
   for (; *value != '\0'; ++value) {
      auto* candidate = value;
      auto* expected = substring;
      while (*candidate != '\0' && *expected != '\0' && *candidate == *expected) {
         ++candidate;
         ++expected;
      }
      if (*expected == '\0') {
         return const_cast<char*>(value);
      }
   }
   return nullptr;
}

char* strerror(int error) {
   return error_name(error);
}

} // extern "C"
