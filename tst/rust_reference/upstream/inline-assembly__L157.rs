// Extracted from src/inline-assembly.md:157
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64;
    let y: i64;
    let z: i64;
    // This
    unsafe { core::arch::asm!("mov {}, {}", out(reg) x, in(reg) 5); }
    // ... this
    unsafe { core::arch::asm!("mov {0}, {1}", out(reg) y, in(reg) 5); }
    // ... and this
    unsafe { core::arch::asm!("mov {out}, {in}", out = out(reg) z, in = in(reg) 5); }
    // all have the same behavior
    assert_eq!(x, y);
    assert_eq!(y, z);
    }
}
