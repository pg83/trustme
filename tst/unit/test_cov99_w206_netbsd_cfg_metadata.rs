//@ crate-type: lib
//@ compile-flags: --target=x86_64-unknown-netbsd --emit=metadata -Cpanic=abort
#![no_std]

pub const NETBSD_CFG: bool = cfg!(all(unix, target_os = "netbsd", target_vendor = "unknown"));
const _: [(); 1] = [(); NETBSD_CFG as usize];

#[cfg(rust_compiler = "trustme")]
const _: [(); 1] = [(); cfg!(netbsd) as usize];
