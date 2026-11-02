//@ compile-fail: asm outputs are not allowed with the `noreturn` option
fn main() { let output: u64; unsafe { core::arch::asm!("ud2 /* {0} */", out(reg) output, options(noreturn)); } }
