//@ proc-macro-aux-build: cov99_w222_owned_tokens.rs
//@ aux-build: cov99_w222_owned_seed.rs
//@ aux-build: cov99_w222_owned_provider.rs

#![feature(arbitrary_self_types_pointers)]

use cov99_w222_owned_provider::{Expose, Export};

impl () {
    fn w222_probe(self: <Export as Expose>::Receiver) {}
}

fn main() {}
