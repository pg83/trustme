//@ compile-fail: Expected '}' in asm! format string
fn main() { unsafe { core::arch::asm!("/* {0:ee} */", in(reg) 7u64); } }
