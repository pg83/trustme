// Extracted from src/inline-assembly.md:1082
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i32 = 0;
    let z: i32;
    // pure can be used to optimize by assuming the assembly has no side effects
    unsafe { core::arch::asm!("inc {}", inout(reg) x => z, options(pure, nomem)); }
    assert_eq!(z, 1);
    }
}
