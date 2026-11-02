//@ compile-fail: asm! format string references undefined value - `missing`
fn main() { unsafe { core::arch::asm!("/* {missing} */", known = in(reg) 7u64); } }
