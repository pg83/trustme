// Extracted from src/inline-assembly.md:130
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    #[unsafe(naked)]
    extern "C" fn wrapper() {
    core::arch::naked_asm!("/* {} */", const 0);
    }
    }
}
