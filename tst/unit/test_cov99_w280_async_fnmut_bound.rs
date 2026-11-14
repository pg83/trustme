//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_trait_bounds)]

pub fn accept_async_mut(_: impl async FnMut(u32) -> u32) {}
