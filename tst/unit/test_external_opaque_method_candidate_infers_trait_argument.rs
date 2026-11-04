//@ aux-build: opaque_method_candidates.rs
extern crate opaque_method_candidates;
use opaque_method_candidates::{first, Stamp, Wrap};
fn main() {
    assert_eq!(Wrap(first()).stamp(), 193);
}
