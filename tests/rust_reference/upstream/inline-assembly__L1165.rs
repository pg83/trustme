// Extracted from src/inline-assembly.md:1165
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64 = 0;
    let z: i64;
    // We can still read from it, though
    unsafe {
        core::arch::asm!("mov {x}, qword ptr [{x}]",
            x = inout(reg) &x => z,
            options(readonly)
        );
    }
    assert_eq!(z, 0);
    }
}
