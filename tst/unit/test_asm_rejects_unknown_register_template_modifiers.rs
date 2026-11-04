//@ compile-fail: invalid asm template modifier
fn main() {
    let mut value = 7u64; unsafe { core::arch::asm!("/* {0:w} */", inout(reg) value); }
}
