//@ crate-type: lib
//@ compile-only
//@ compile-flags: -C panic=abort -C emit-cpp-only --target $DIR/emulated_i128_target.toml
#![feature(core_intrinsics, f128)]
#![no_std]

#[no_mangle]
pub unsafe fn cov99_w1486_f128_to_i128(value: f128) -> i128 {
    core::intrinsics::float_to_int_unchecked::<f128, i128>(value)
}
