//@ compile-fail: missing from the explicit --crate table
//@ aux-build: coverage_identity_leaf.rs
//@ aux-search: coverage_identity_leaf
//@ aux-output-dir: coverage_identity_leaf.rs=t
//@ aux-build: coverage_identity_parent.rs
//@ aux-output-dir: coverage_identity_parent.rs=t
//@ compile-flags: --crate coverage_identity_parent=$OUT/libcoverage_identity_parent.rlib
#![feature(no_core)]
#![no_core]

extern crate coverage_identity_parent;

fn main() {}
