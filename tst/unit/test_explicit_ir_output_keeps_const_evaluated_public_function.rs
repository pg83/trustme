//@ crate-type: lib
//@ compile-flags: --emit=llvm-ir=$OUT.ll
//@ output-contains: .ll checked_value
//@ compile-variant: --emit=llvm-ir=$OUT.ll -Ctarget-feature=-crt-static,-crt-static
const VALUE: u8 = 37;
const _: () = assert!(VALUE % 5 == 2);
#[no_mangle]
pub extern "C" fn checked_value() -> u8 { VALUE % 5 }
