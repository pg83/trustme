// Extracted from src/inline-assembly.md:392
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x: i64 = 4;
    // `inlateout` is `inout` using `lateout`
    unsafe { core::arch::asm!("inc {}", inlateout(reg) x); }
    assert_eq!(x, 5);
    }
}
