//@ compile-fail: asm! options `nomem` and `readonly` are mutually exclusive
fn main() { unsafe { core::arch::asm!("nop", options(nomem, readonly)); } }
