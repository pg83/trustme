// Extracted from src/inline-assembly.md:964
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x = 0x10u16;
    
    // u16::swap_bytes using `xchg`
    // low half of `{x}` is referred to by `{x:l}`, and the high half by `{x:h}`
    unsafe { core::arch::asm!("xchg {x:l}, {x:h}", x = inout(reg_abcd) x); }
    assert_eq!(x, 0x1000u16);
    }
}
