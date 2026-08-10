// Extracted from src/inline-assembly.md:315
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // ``in` can be used to pass values into inline assembly...
    unsafe { core::arch::asm!("/* {} */", in(reg) 5); }
    }
}
