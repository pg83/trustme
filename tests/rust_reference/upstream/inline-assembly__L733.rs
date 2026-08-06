// Extracted from src/inline-assembly.md:733
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x: i64;
    // Moving a 32-bit value into a 64-bit value, oops.
    #[allow(asm_sub_register)] // rustc warns about this behavior
    unsafe { core::arch::asm!("mov {}, {}", lateout(reg) x, in(reg) 4i32); }
    // top 32-bits are indeterminate
    assert_eq!(x, 4); // This assertion is not guaranteed to succeed
    assert_eq!(x & 0xFFFFFFFF, 4); // However, this one will succeed
    }
}
