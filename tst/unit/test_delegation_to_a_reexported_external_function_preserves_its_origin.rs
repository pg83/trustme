//@ aux-build: delegated_free_signatures.rs
//@ aux-build: delegated_free_reexports.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_free_reexports::selected as forwarded;

fn main() {
    assert_eq!(forwarded([167, 173, 179]), [173, 179, 167]);
}
