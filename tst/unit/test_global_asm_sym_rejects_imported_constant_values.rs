//@ aux-build: asm_operand_exports.rs
//@ compile-fail: function or static
core::arch::global_asm!("/* {symbol} */", symbol = sym asm_operand_exports::CONSTANT);
fn main() {}
