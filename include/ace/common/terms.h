#ifndef ACE_COMMON_TERMS_H
#define ACE_COMMON_TERMS_H

#define ACE_CONDUCTOR_MEM_SIZE std::hardware_constructive_interference_size // Size of cacheline

#define ACE_CACHE_LINE_SIZE std::hardware_constructive_interference_size // Size of cacheline

#define ACE_CACHE_LINE(number) [[maybe_unused]] struct {} _cache_line_##number[0] {};

typedef struct {} ACE_EMPTY_TYPE;

#endif // ACE_COMMON_TERMS_H
