//@ compile-fail: invalid left-hand side of assignment
fn main() { unsafe { std::arch::asm!("nop") = (); } }
