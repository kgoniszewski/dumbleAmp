# Compiler settings for targets that contain the real-time DSP.
#
# We deliberately do NOT use -ffast-math: the engine relies on std::isfinite() as a last line
# of defence against NaN/Inf, which -ffinite-math-only would silently remove. The flags below
# give most of the speed (no errno, FMA contraction) without changing IEEE semantics.
function(dumble_apply_dsp_flags target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -fno-math-errno
            -ffp-contract=fast
            $<$<CONFIG:Release>:-O3>
            -Wall -Wextra -Wshadow -Wno-unused-parameter)
    endif()

    if(DUMBLE_RTSAN)
        # RealtimeSanitizer: flags any allocation/lock/syscall inside [[clang::nonblocking]] code.
        target_compile_options(${target} PRIVATE -fsanitize=realtime)
        target_link_options(${target} PRIVATE -fsanitize=realtime)
        target_compile_definitions(${target} PRIVATE DUMBLE_RTSAN=1)
    endif()
endfunction()
