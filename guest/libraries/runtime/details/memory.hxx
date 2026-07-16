#include <cstddef>
#include <cstdint>

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

} // extern "C"
