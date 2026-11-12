//@ aux-build: own_rpit_trait_alias.rs
//@ compile-fail: MissingType
#![feature(trait_alias)]

use own_rpit_trait_alias::Sources;

fn require<S: Sources<MissingType = u16>>() {}

fn main() {}
