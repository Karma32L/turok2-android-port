# Flags estrictos compartidos por nuestro código (nunca se imponen a terceros)
function(turok2_strict target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror=format-security
        -fstack-protector-strong
        $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>)
endfunction()
