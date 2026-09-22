# SnakeOS v0.00
x86-64 OS GRUB based

## Features
- IDT with Handlers
- Jump to Long mode
- VGA Text Driver
- PIC
- PIT (IRQ0)
- PS/2 Keyboard (IRQ1)
- PMM
- VMM
- Heap
- Scheduler
- ring 3
- syscalls

## Setup
- Clone this repo.
```
git clone https://github.com/SnakeOS-dev/SnakeOS-src
cd SnakeOS-src
```
- Run the kernel.
```
make run
```
## Requirements
- nasm
- clang (with `x86_64-elf` target)
- ld.lld
- grub-mkrescue (grub-pc-bin, grub-common, xorriso, mtools)
- qemu-system-x86_64

On Debian/Ubuntu:
```

sudo apt install nasm clang lld grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

## License
0BSD - see [LICENSE](LICENSE).
