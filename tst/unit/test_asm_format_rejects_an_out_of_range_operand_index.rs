//@ compile-fail: asm! format string index out of range
fn main() { unsafe { core::arch::asm!("/* {1} */", in(reg) 7u64); } }
