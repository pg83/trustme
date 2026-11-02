//@ compile-fail: Unsupported clobber ABI `Rust` for x86-64
fn main() { unsafe { core::arch::asm!("nop", clobber_abi("Rust")); } }
