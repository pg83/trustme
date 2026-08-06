// Extracted from src/inline-assembly.md:1109
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x = 0i32;
    let z: i32;
    // Accessing outside memory from assembly when `nomem` is
    // specified is disallowed
    unsafe {
        core::arch::asm!("mov {val:e}, dword ptr [{ptr}]",
            ptr = in(reg) &mut x,
            val = lateout(reg) z,
            options(nomem)
        )
    }
    
    // Writing to outside memory from assembly when `nomem` is
    // specified is also undefined behaviour
    unsafe {
        core::arch::asm!("mov  dword ptr [{ptr}], {val:e}",
            ptr = in(reg) &mut x,
            val = in(reg) z,
            options(nomem)
        )
    }
    }
}
