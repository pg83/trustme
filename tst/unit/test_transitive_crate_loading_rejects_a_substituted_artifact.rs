//@ compile-fail: didn't load the expected crate
//@ aux-build: coverage_identity_leaf.rs
//@ aux-output-dir: coverage_identity_leaf.rs=t
//@ aux-build: coverage_identity_parent.rs
//@ aux-output-dir: coverage_identity_parent.rs=t
//@ aux-build: coverage_identity_imposter.rs
//@ aux-output-dir: coverage_identity_imposter.rs=t
//@ compile-flags: --extern coverage_identity_leaf=$OUT/libcoverage_identity_imposter.rlib
#![feature(no_core)]
#![no_core]

extern crate coverage_identity_parent;

fn main() {}
