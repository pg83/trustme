//@ aux-build: imported_crate_root_routes.rs
//@ compile-variant: -O

pub extern crate imported_crate_root_routes as base;
use base as alias;
use alias as chain;

mod bridge { pub use crate::base as exported; }

fn main() {
    let first: alias::Token = chain::Token(alias::VALUE);
    let second = bridge::exported::Token(bridge::exported::nested::value());
    assert_eq!(first.0, 317);
    assert_eq!(second.0, 419);
}
