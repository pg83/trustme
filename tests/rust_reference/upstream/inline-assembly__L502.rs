// Extracted from src/inline-assembly.md:502
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut y: i64;
    // We can name both `reg`, or an explicit register like `eax` to get an
    // integer register
    unsafe { core::arch::asm!("mov eax, {:e}", in(reg) 5, lateout("eax") y); }
    assert_eq!(y, 5);
    }
}
