# Shared compile options applied to every first-party target via rvn_apply_options().

add_library(rvn_options INTERFACE)

if(MSVC)
    target_compile_options(rvn_options INTERFACE /W4 /permissive-)
    if(RVN_WARNINGS_AS_ERRORS)
        target_compile_options(rvn_options INTERFACE /WX)
    endif()
else()
    target_compile_options(rvn_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough)
    if(RVN_WARNINGS_AS_ERRORS)
        target_compile_options(rvn_options INTERFACE -Werror)
    endif()
endif()

if(RVN_SANITIZE)
    message(STATUS "Sanitizers enabled: ${RVN_SANITIZE}")
    target_compile_options(rvn_options INTERFACE -fsanitize=${RVN_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(rvn_options INTERFACE -fsanitize=${RVN_SANITIZE})
endif()
