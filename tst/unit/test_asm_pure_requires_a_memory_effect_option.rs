//@ compile-fail: asm! marked `pure` without `nomem` or `readonly`
fn main() { let output: u64; unsafe { core::arch::asm!("mov {0}, 13", out(reg) output, options(pure)); } }
