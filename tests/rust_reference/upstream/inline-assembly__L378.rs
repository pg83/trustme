// Extracted from src/inline-assembly.md:378
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64;
    // `inout` can also move values to different places
    unsafe { core::arch::asm!("inc {}", inout(reg) 4u64=>x); }
    assert_eq!(x, 5);
    }
}
