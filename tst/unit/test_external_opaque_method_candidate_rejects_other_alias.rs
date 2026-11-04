//@ compile-fail: error
//@ aux-build: opaque_method_candidates.rs
extern crate opaque_method_candidates;
use opaque_method_candidates::{second, Stamp, Wrap};
fn main() {
    let _ = Wrap(second()).stamp();
}
