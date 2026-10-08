//@ aux-build: namesake.rs
//@ compile-flags: --crate-name namesake
// `::name` names a crate of the extern prelude. From the 2018 edition on that
// is never the crate being compiled, even one of the same name (only
// `extern crate self as name;` would put it there). async-compression's
// integration test tests/proptest.rs, a crate named `proptest`, writes
// `use ::proptest::{prop_oneof, ..};` for the proptest crate.
use ::namesake::{seven, Marker};

fn main() {
    assert_eq!(seven!(), 7);
    assert_eq!(Marker(1).0, 1);
}
