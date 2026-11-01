//@ compile-fail: function or static
enum Choice { Empty, Value(u8) }
fn main() {
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym Choice::Empty); }
}
