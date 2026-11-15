//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ aux-build: cov99_w1122_edition2018_macros.rs

pub fn panic_from_external_definition() -> ! {
    cov99_w1122_edition2018_macros::panic_from_definition!()
}

pub fn unreachable_from_external_definition() -> ! {
    cov99_w1122_edition2018_macros::unreachable_from_definition!()
}
