//@ aux-build: cov99_w911_clone_factory.rs
//@ compile-only
//@ compile-flags: -C opt-level=2 -C codegen-type=monomir -Z dump-mir

use cov99_w911_clone_factory::make;

fn replace<F: Fn() -> u32 + Clone>(mut destination: F, source: &F, use_clone_from: bool) -> F {
    if use_clone_from {
        destination.clone_from(source);
    } else {
        destination = source.clone();
    }
    destination
}

fn main() {
    let source = make(11);
    assert_eq!(replace(make(7), &source, true)(), 11);
    assert_eq!(replace(make(7), &source, false)(), 11);
}
