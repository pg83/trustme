//@ compile-fail: function or static
const VALUE: u32 = 23;
core::arch::global_asm!("/* {symbol} */", symbol = sym VALUE);
fn main() {}
