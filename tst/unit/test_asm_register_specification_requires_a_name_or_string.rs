//@ compile-fail: Unexpected token
fn main() { unsafe { core::arch::asm!("/* {0} */", in(23) 7u64); } }
