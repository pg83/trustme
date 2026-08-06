// Extracted from src/inline-assembly.md:332
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64;
    // and `out` can be used to pass values back to rust.
    unsafe { core::arch::asm!("/* {} */", out(reg) x); }
    }
}
