//@ compile-fail: invalid asm template modifier
fn main() {
    unsafe { core::arch::asm!("/* {0:h} */", in(reg) 11u64); }
}
