// Extracted from src/inline-assembly.md:879
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // bp is reserved
    unsafe { core::arch::asm!("", in("bp") 5i32); }
    // ERROR: invalid register `bp`: the frame pointer cannot be used as an operand for inline asm
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
