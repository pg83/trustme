//@ edition: 2021
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ aux-build: cov99_w1656_value_shadow.rs

#![no_std]

use cov99_w1656_value_shadow::Marker;

pub fn identity(value: Marker) -> Marker {
    value
}
