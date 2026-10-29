//@ compile-flags: -O
use std::arch::asm;
fn main() {
    let mut bits = 0b1010u32;
    let address = &mut bits as *mut u32;
    let old: u8;
    unsafe { asm!("btl {1:e}, ({0})", "setc {2}", in(reg) address, in(reg) 1u32, out(reg_byte) old, options(att_syntax, nostack)); }
    assert_eq!(old, 1);
    let old: u8;
    unsafe { asm!("btcl {1:e}, ({0})", "setc {2}", in(reg) address, in(reg) 1u32, out(reg_byte) old, options(att_syntax, nostack)); }
    assert_eq!((old, bits), (1, 8));
    let old: u8;
    unsafe { asm!("btrl {1:e}, ({0})", "setc {2}", in(reg) address, in(reg) 3u32, out(reg_byte) old, options(att_syntax, nostack)); }
    assert_eq!((old, bits), (1, 0));
    let old: u8;
    unsafe { asm!("btsl {1:e}, ({0})", "setc {2}", in(reg) address, in(reg) 2u32, out(reg_byte) old, options(att_syntax, nostack)); }
    assert_eq!((old, bits), (0, 4));
}
