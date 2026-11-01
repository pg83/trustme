//@ compile-fail: function or static
fn emit<const N: usize>() {
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym N); }
}
fn main() { emit::<3>(); }
