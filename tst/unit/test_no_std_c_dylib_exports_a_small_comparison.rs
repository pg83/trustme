//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Cpanic=abort
//@ compile-variant: --crate-type=cdylib
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! { loop {} }

#[no_mangle]
pub extern "C" fn orchard_select(first: u32, second: u32) -> u32 {
    if first > second { first } else { second }
}
