//@ crate-type: lib
//@ compile-flags: --cfg syn_disable_nightly_tests

#![cfg(not(syn_disable_nightly_tests))]

extern crate trustme_intentionally_missing_crate;
