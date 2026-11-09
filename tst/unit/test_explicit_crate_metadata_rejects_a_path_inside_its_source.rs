//@ compile-fail: Unable to open crate
//@ compile-flags: --crate coverage_absent=$DIR/test_explicit_crate_metadata_rejects_a_path_inside_its_source.rs/child
#![feature(no_core)]
#![no_core]

extern crate coverage_absent;

fn main() {}
