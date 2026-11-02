//@ compile-fail: Unknown register for x86/x86-64
fn main() { unsafe { core::arch::asm!("/* {0} */", in(no_such_register_class) 7u64); } }
