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
char* strcpy(char* destination, const char* source);
char* strncpy(char* destination, const char* source, std::size_t size);
char* strcat(char* destination, const char* source);
char* strncat(char* destination, const char* source, std::size_t size);
char* strchr(const char* value, int character);
char* strrchr(const char* value, int character);
std::size_t strspn(const char* value, const char* accepted);
std::size_t strcspn(const char* value, const char* rejected);
char* strpbrk(const char* value, const char* accepted);
char* strstr(const char* value, const char* substring);
char* strerror(int error);
}
