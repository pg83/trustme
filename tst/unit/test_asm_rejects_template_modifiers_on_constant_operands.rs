//@ compile-fail: invalid asm template modifier
fn main() {
    unsafe { core::arch::asm!("/* {number:e} */", number = const 17); }
}
