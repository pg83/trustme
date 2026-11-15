//@ stdin-source
//@ crate-type: lib
//@ compile-flags: --crate-name cov99_w1389_stdin_include --emit=metadata
#![no_std]

pub const SOURCE: &str = include_str!("tst/unit/test_cov99_w1389_stdin_include.rs");
