//@ compile-fail: invalid asm template modifier
fn main() {
    unsafe { core::arch::asm!("/* {0:r} */", in(xmm_reg) 1.75f64); }
}
