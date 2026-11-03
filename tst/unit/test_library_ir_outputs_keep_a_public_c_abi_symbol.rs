//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=llvm-ir=$OUT.ll
//@ compile-variant: --crate-type=dylib
//@ compile-variant: --crate-type=cdylib
//@ output-contains: .ll orchard_library_ir_value
#[no_mangle]
pub extern "C" fn orchard_library_ir_value(value: u64) -> u64 {
    value.rotate_left(7) ^ 0x35
}
