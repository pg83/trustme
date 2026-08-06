// Extracted from src/inline-assembly.md:1234
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // `push` and `pop` are UB when used with nostack
    unsafe { core::arch::asm!("push rax", "pop rax", options(nostack)); }
    }
}
