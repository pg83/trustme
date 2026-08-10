// Extracted from src/inline-assembly.md:1213
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // You are responsible for not falling past the end of a noreturn asm block
    unsafe { core::arch::asm!("", options(noreturn)); }
    }
}
