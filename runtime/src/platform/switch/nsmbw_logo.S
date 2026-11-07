    .section .rodata
    .balign 16
    .global nsmbw_logo_rgba
    .type nsmbw_logo_rgba, %object
nsmbw_logo_rgba:
    .incbin "nsmbw_logo.rgba"
    .global nsmbw_logo_rgba_end
nsmbw_logo_rgba_end:
    .size nsmbw_logo_rgba, . - nsmbw_logo_rgba
