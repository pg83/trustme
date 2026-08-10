// Extracted from src/inline-assembly.md:463
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut y: i64;
    // y gets its value from the second output, rather than the first
    unsafe { core::arch::asm!("mov {}, 0", "mov {}, 1", out(reg) y, out(reg) y); }
    assert_eq!(y, 1);
    }
}
