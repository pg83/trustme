//@ compile-fail: asm! requires a string literal
fn main() { unsafe { core::arch::asm!("/* {0} */", middle(reg) 7u64); } }
