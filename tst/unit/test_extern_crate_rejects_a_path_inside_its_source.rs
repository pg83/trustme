//@ compile-fail: Unable to open crate
//@ compile-flags: --extern coverage_absent=$DIR/test_extern_crate_rejects_a_path_inside_its_source.rs/child

extern crate coverage_absent;

fn main() {}
