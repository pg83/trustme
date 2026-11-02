//@ compile-fail: asm with `clobber_abi` must specify explicit registers for outputs
fn main() { let output: u64; unsafe { core::arch::asm!("mov {0}, 13", out(reg) output, clobber_abi("C")); } }
