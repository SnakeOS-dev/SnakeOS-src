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

    end = ALIGN(4K);

    /DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
