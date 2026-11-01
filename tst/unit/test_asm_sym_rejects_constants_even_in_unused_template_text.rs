//@ compile-fail: function or static
const VALUE: u32 = 17;
fn main() {
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym VALUE); }
}
