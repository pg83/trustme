//@ compile-fail: expected 'other_identity'
//@ aux-build: coverage_identity_leaf.rs
//@ aux-output-dir: coverage_identity_leaf.rs=t
//@ compile-flags: --crate other_identity=$OUT/libcoverage_identity_leaf.rlib
#![feature(no_core)]
#![no_core]

extern crate other_identity;

fn main() {}
