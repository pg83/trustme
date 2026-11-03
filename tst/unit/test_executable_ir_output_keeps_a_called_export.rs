//@ compile-only
//@ compile-flags: --emit=llvm-ir=$OUT.ll
//@ output-contains: .ll orchard_executable_ir_value
#[no_mangle]
pub extern "C" fn orchard_executable_ir_value(value: u32) -> u32 { value.wrapping_add(17) }
fn main() { assert_eq!(orchard_executable_ir_value(223), 240); }
