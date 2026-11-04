//@ compile-fail: invalid asm template modifier
fn main() {
    unsafe { core::arch::asm!("/* {0:e} */", in(reg_byte) 13u8); }
}
