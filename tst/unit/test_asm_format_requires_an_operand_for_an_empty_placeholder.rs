//@ compile-fail: asm! format doesn't have enough arguments
fn main() { unsafe { core::arch::asm!("mov {}, 17"); } }
