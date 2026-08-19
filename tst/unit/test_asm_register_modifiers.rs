//@ run-pass
// Rust names the part of a register an operand wants by its width -- `l` for
// the low byte, `h` for the second, `x` for the low half, `e` and `r` for the
// wider ones. GCC names the same parts by other letters, so each has to be
// translated rather than passed through.

#![cfg(target_arch = "x86_64")]

fn main() {
    // u16::swap_bytes, using the low and second bytes of one register
    let mut halves = 0x10u16;
    unsafe {
        core::arch::asm!("xchg {x:l}, {x:h}", x = inout(reg_abcd) halves);
    }
    assert_eq!(halves, 0x1000u16);

    // The low half of a register, and the 32-bit and 64-bit views of one.
    let mut narrow = 0u16;
    unsafe {
        core::arch::asm!("mov {v:x}, 7", v = out(reg) narrow);
    }
    assert_eq!(narrow, 7);

    let mut middle = 0u32;
    unsafe {
        core::arch::asm!("mov {v:e}, 9", v = out(reg) middle);
    }
    assert_eq!(middle, 9);

    let mut wide = 0u64;
    unsafe {
        core::arch::asm!("mov {v:r}, 11", v = out(reg) wide);
    }
    assert_eq!(wide, 11);
}
