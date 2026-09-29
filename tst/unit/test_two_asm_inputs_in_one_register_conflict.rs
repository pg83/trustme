//@ compile-fail: register `cl` conflicts with register `rcx`
// Two inputs overlapping in one physical register still conflict, `rcx` and
// its low byte `cl` included (`lower_inline_asm`'s input set).
use std::arch::asm;

fn main() {
    let hi: u64 = 1;
    let lo: u8 = 2;
    unsafe {
        asm!("", in("rcx") hi, in("cl") lo);
    }
}
