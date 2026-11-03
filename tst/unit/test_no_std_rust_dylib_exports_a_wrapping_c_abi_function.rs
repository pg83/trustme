//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Cpanic=abort
//@ compile-variant: --crate-type=dylib
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! { loop {} }

#[no_mangle]
pub extern "C" fn orchard_mix(value: u32) -> u32 {
    value.wrapping_mul(17).wrapping_add(3)
}
