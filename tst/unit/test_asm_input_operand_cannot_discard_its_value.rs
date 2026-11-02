//@ compile-fail: Invalid use of _ in asm!
fn main() { unsafe { core::arch::asm!("/* {0} */", in(reg) _); } }
