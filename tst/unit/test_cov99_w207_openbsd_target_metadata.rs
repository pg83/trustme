//@ crate-type: lib
//@ compile-flags: --target x86_64-unknown-openbsd --emit=metadata -Cpanic=abort
#![feature(no_core)]
#![no_core]

#[cfg(not(all(target_os = "openbsd", target_family = "unix", target_vendor = "unknown")))]
compile_error!("expected the built-in OpenBSD target configuration");

pub mod openbsd_target {}
