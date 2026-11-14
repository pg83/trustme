//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(cfg_version)]

fn main() {
    let _: [(); 0] = [(); cfg!(version("invalid")) as usize];
}
