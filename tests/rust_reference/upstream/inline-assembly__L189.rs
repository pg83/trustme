// Extracted from src/inline-assembly.md:189
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64;
    let y: i64;
    // We can separate multiple strings as if they were written together
    unsafe { core::arch::asm!("mov eax, 5", "mov ecx, eax", out("rax") x, out("rcx") y); }
    assert_eq!(x, y);
    }
}
