//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Looking up a non-namespace, but pointed to crate root

#![no_std]

pub type Root = ::core;
