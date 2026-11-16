    .section .rodata
    .balign 16
    .global nsmbw_boot_logo_png
    .type nsmbw_boot_logo_png, %object
nsmbw_boot_logo_png:
    .incbin "boot_logo.png"
    .global nsmbw_boot_logo_png_end
nsmbw_boot_logo_png_end:
    .size nsmbw_boot_logo_png, nsmbw_boot_logo_png_end - nsmbw_boot_logo_png
