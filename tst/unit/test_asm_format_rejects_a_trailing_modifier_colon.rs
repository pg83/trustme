//@ compile-fail: Unexpected EOF in asm! format string
fn main() { unsafe { core::arch::asm!("/* {0:", in(reg) 7u64); } }
