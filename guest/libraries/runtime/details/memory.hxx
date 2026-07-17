#pragma once

#include <cstddef>

extern "C" {

void* memset(void* destination, int value, std::size_t size);
void* memcpy(void* destination, const void* source, std::size_t size);
void* memmove(void* destination, const void* source, std::size_t size);
int memcmp(const void* left, const void* right, std::size_t size);
void* memchr(const void* source, int value, std::size_t size);
std::size_t strlen(const char* value);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, std::size_t size);
}
