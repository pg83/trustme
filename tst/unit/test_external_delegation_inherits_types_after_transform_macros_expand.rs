//@ aux-build: type_phase_delegation.rs
#![feature(fn_delegation)]
#![allow(incomplete_features)]

use type_phase_delegation::{Operations, Source};
struct Wrapper(Source);

impl Operations for Wrapper {
    reuse Operations::choose { { assert!(true); self.0 } }
}

fn main() {
    assert_eq!(Wrapper(Source).choose(31u16, false), (31, true));
    assert_eq!(Wrapper(Source).choose("path", true), ("path", false));
}
