// Extracted from src/inline-assembly.md:121
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    unsafe { core::arch::asm!("/* {} */", in(reg) 0); }
    }
}
