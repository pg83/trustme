//@ aux-build: enum_value_path_exports.rs
//@ compile-fail: function or static
fn main() {
    unsafe { core::arch::asm!("/* {symbol} */", symbol = sym enum_value_path_exports::ACTIVE); }
}
