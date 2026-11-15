//@ crate-type: lib
//@ compile-flags: --cfg ' cov99_w1666 = "accepted" '
#![no_std]

#[cfg(not(cov99_w1666 = "accepted"))]
compile_error!("spaced --cfg value was not set");

#[cfg(cov99_w1666 = "accepted")]
pub fn selected() -> u16 {
    1666
}
