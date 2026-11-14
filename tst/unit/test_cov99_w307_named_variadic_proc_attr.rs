//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ proc-macro-aux-build: cov99_w307_named_variadic_attr.rs

#![feature(c_variadic)]

#[cov99_w307_named_variadic_attr::erase]
unsafe extern "C" fn named_variadic(fixed: i32, args: ...) {}
