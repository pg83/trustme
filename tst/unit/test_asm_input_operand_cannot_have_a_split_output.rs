//@ compile-fail: Invalid use of => in asm!
fn main() { let result: u64; unsafe { core::arch::asm!("/* {0} */", in(reg) 7u64 => result); } }
