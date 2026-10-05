#pragma once

// Marks functions that run on the audio thread.
//
// With LLVM clang >= 20 and -fsanitize=realtime (CMake option DUMBLE_RTSAN), RealtimeSanitizer
// aborts with a stack trace on any allocation, lock, or blocking syscall reached from a function
// carrying [[clang::nonblocking]]. On other compilers this expands to nothing.
#if defined (DUMBLE_RTSAN) && defined (__has_cpp_attribute)
 #if __has_cpp_attribute (clang::nonblocking)
  #define DUMBLE_NONBLOCKING [[clang::nonblocking]]
 #endif
#endif

#ifndef DUMBLE_NONBLOCKING
 #define DUMBLE_NONBLOCKING
#endif
