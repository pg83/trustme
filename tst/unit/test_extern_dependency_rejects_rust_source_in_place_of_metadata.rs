//@ input: aux/metadata_source_dep.rs
//@ compile-flags: --extern metadata_source_dep=$DIR/aux/metadata_source_dep.rs
//@ compile-fail: error: (code 2,

extern crate metadata_source_dep;

fn main() {
    assert_eq!(metadata_source_dep::VALUE, 41);
}
