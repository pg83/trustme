// Extracted from src/inline-assembly.md:345
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64;
    // `lateout` is the same as `out`
    // but the compiler knows we don't care about the value of any inputs by the
    // time we overwrite it.
    unsafe { core::arch::asm!("mov {}, 5", lateout(reg) x); }
    assert_eq!(x, 5)
    }
}
