// Extracted from src/inline-assembly.md:759
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i32 = 0;
    let y: f32;
    // But we can't reinterpret an `i32` to an `f32` like this
    unsafe { core::arch::asm!("/* {} */", inout(reg) x=>y); }
    // ERROR: incompatible types for asm inout argument
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
