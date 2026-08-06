// Extracted from src/inline-assembly.md:1244
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i32;
    let y = 1i32;
    // We need to use AT&T Syntax here. src, dest order for operands
    unsafe {
        core::arch::asm!("mov {y:e}, {x:e}",
            x = lateout(reg) x,
            y = in(reg) y,
            options(att_syntax)
        );
    }
    assert_eq!(x, y);
    }
}
