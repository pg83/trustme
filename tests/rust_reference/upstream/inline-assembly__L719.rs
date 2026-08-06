// Extracted from src/inline-assembly.md:719
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let z = unsafe { core::arch::x86_64::_mm_set_epi64x(1, 0) };
    // We can't pass an `__m128i` to a `reg` input
    unsafe { core::arch::asm!("/* {} */", in(reg) z); }
    // ERROR: type `__m128i` cannot be used with this register class
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
