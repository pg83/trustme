//@ aux-build: own_rpit_trait_alias.rs
//@ compile-fail: missing_callback
#![feature(return_type_notation, trait_alias)]

use own_rpit_trait_alias::Sources;

fn require<S: Sources<missing_callback(..): Copy>>() {}

fn main() {}
