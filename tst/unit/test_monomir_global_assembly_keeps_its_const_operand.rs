//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only
//@ output-contains: .mir own_monomir_asm_byte
#![no_std]

core::arch::global_asm!(
    ".pushsection .rodata",
    ".globl own_monomir_asm_byte",
    "own_monomir_asm_byte:",
    ".byte {value}",
    ".popsection",
    value = const 73u8,
);

#[no_mangle]
pub fn retained_rust_value() -> u8 {
    19
}
