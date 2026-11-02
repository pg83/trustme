//@ compile-fail: Closing braces in `asm!` need to be written as `}}`
fn main() { unsafe { core::arch::asm!("nop } x"); } }
