//@ edition: 2021
//@ proc-macro-aux-build: cov99_w438_use_root.rs

#[cov99_w438_use_root::keep]
use ::core as root_core;

fn main() {
    assert_eq!(root_core::mem::size_of::<u16>(), 2);
}
