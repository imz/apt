#ifndef APT_EXTRA_UTILS_H
#define APT_EXTRA_UTILS_H

#include <stddef.h>

template <size_t N>
constexpr size_t get_minimal_power_of_2_helper(size_t size)
{
   return (size <= N) ? N : get_minimal_power_of_2_helper<N*2>(size);
}

constexpr size_t get_minimal_power_of_2(size_t size)
{
   return get_minimal_power_of_2_helper<1>(size);
}

#endif /* APT_EXTRA_UTILS_H */
