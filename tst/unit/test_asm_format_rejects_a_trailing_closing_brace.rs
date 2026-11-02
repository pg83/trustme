//@ compile-fail: Unexpected EOF in asm! format string
fn main() { unsafe { core::arch::asm!("nop }"); } }
