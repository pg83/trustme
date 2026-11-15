//@ crate-type: lib
//@ compile-flags: --target aarch64-unknown-freebsd -Z stop-after=expand

#![feature(no_core)]
#![no_core]

#[cfg(not(all(
    target_arch = "aarch64",
    target_os = "freebsd",
    target_family = "unix",
    target_env = "gnu",
    target_pointer_width = "64",
    target_endian = "little"
)))]
compile_error!("expected the aarch64-unknown-freebsd target configuration");
