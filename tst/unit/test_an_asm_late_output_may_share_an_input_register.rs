// portable-atomic's `cmpxchg16b` takes `in("rcx") new.pair.hi` and returns its
// flag in `lateout("cl") r`. rustc checks inputs and outputs for overlap
// separately (`lower_inline_asm`): `in` claims an input register, a late
// output an output register, `out` and `inout` both - a late output is
// written only after every input is read, so it may share one.
use std::arch::asm;

fn main() {
    let hi: u64 = 0x1ff;
    let low_byte: u8;
    unsafe {
        asm!("mov {0}, rcx", "mov cl, 1", out(reg) _, in("rcx") hi, lateout("cl") low_byte);
    }
    assert_eq!(low_byte, 1);
}
