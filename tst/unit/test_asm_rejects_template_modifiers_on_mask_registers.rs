//@ compile-fail: invalid asm template modifier
#[target_feature(enable = "avx512f")]
unsafe fn probe() { core::arch::asm!("/* {0:x} */", in(kreg) 1u16); }
fn main() { unsafe { probe(); } }
