//@ compile-fail: option cannot be used

core::arch::global_asm!("", options(nostack));

fn main() {}
