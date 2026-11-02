//@ compile-fail: positional arguments cannot follow named arguments
fn main() { let first = 3u64; let second = 5u64; unsafe { core::arch::asm!("/* {named} {1} */", named = in(reg) first, in(reg) second); } }
