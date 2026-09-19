ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text :
    {
        *(.multiboot)
        *(.text.prologue)
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(COMMON)
        *(.bss)
    }
    . = ALIGN(4096);
    kernel_end = .;
    /DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
