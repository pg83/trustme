// Extracted from src/inline-assembly.md:362
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x: i64 = 4;
    // `inout` can be used to modify values in-register
    unsafe { core::arch::asm!("inc {}", inout(reg) x); }
    assert_eq!(x, 5);
    }
}
