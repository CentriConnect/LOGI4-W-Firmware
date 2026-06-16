#ifndef SYSTEM_STRINGLENGTH_H
#define SYSTEM_STRINGLENGTH_H

#include "System/System.h"

/// Finds the length of a string by searching for a null-terminating character. This is
/// basically an implementation of strlen that provides a max length to search.
/// @param data String to find the length of
/// @param maxLength Maximum number of bytes to search
/// @returns The length of the string, without the null terminating character. Returns maxLength if
///     no null character is found.
static inline uint8 StringLength(const char* data, uint8 maxLength)
{
  uint8 result = 0U;
  for (result = 0U; result < maxLength; result++)
  {
    if (data[result] == 0)
    {
      break;
    }
  }
  return result;
}

#endif // SYSTEM_STRINGLENGTH_H
