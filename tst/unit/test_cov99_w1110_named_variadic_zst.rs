//@ compile-only
//@ crate-type: lib
//@ compile-flags: -C emit-cpp-only -C panic=abort
//@ output-contains: .cpp va_start(*(va_list*)&arg1, 0);
#![feature(c_variadic)]

pub unsafe extern "C" fn named_variadic_zst(_: (), _args: ...) {}
