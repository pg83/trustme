//@ edition: 2015
//@ aux-build: cov99_w1166_trait_inherent_aux.rs

#![allow(bare_trait_objects)]

extern crate cov99_w1166_trait_inherent_aux;

fn main() {
    assert_eq!(cov99_w1166_trait_inherent_aux::Signal::answer(), 37);
}
