//@ crate-type: lib
//@ aux-build: cov99_w812_other_panic_runtime.rs
//@ aux-output-dir: cov99_w812_other_panic_runtime.rs=t.aux
//@ compile-flags: --emit=metadata -Cpanic=abort --crate-object cov99_w812_other_panic_runtime=$OUT.aux/libcov99_w812_other_panic_runtime.rlib.o -Cemit-link-manifest=$OUT.manifest

#![no_std]

extern crate cov99_w812_other_panic_runtime;

pub fn marker() -> u8 {
    cov99_w812_other_panic_runtime::marker()
}
