//@ compile-fail: function or static
fn target() {}
fn main() {
    let pointer: fn() = target;
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym pointer); }
}
