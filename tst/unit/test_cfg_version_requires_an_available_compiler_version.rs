//@ compile-fail: cfg(version) with no rustc version given
//@ env: RUSTC_OVERRIDE_VERSION_STRING=
#![feature(no_core)]
#![no_core]

#[cfg(version("1.82"))]
fn gated() {}

fn main() {}
