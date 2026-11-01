//@ compile-fail: function or static
struct Unit;
fn main() {
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym Unit); }
}
