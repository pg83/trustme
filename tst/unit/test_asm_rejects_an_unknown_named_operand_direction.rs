//@ compile-fail: Unknown asm fragment - `middle`
fn main() { unsafe { core::arch::asm!("/* {bad} */", bad = middle(reg) 7u64); } }
