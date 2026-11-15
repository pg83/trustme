//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(cfg_version)]

#[cfg(version("1"))]
compile_error!("a single version component must not satisfy cfg(version)");
