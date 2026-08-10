// Extracted from src/inline-assembly.md:177
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x = 5;
    // We can't refer to `x` from the scope directly, we need an operand like `in(reg) x`
    unsafe { core::arch::asm!("/* {x} */"); } // ERROR: no argument named x
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
