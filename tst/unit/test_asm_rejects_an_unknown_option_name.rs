//@ compile-fail: Unknown asm option - does_not_touch_memory
fn main() { unsafe { core::arch::asm!("nop", options(does_not_touch_memory)); } }
