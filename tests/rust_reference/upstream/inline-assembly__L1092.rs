// Extracted from src/inline-assembly.md:1092
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i32 = 0;
    let z: i32;
    // Either nomem or readonly must be satisfied, to indicate whether or not
    // memory is allowed to be read
    unsafe { core::arch::asm!("inc {}", inout(reg) x => z, options(pure)); }
    // ERROR: the `pure` option must be combined with either `nomem` or `readonly`
    assert_eq!(z, 0);
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
