// Extracted from src/inline-assembly.md:1293
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let z: i32;
    // noreturn can't have outputs
    unsafe { core::arch::asm!("mov {:e}, 1", out(reg) z, options(noreturn)); }
    // ERROR: asm outputs are not allowed with the `noreturn` option
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
