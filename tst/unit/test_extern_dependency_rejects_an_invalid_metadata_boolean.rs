//@ input: support/metadata/own_invalid_bool.metadata
//@ input: support/metadata/invalid_dependency_packets.py
//@ compile-flags: --extern own_invalid_bool=$DIR/support/metadata/own_invalid_bool.metadata
//@ compile-fail: Expected false

extern crate own_invalid_bool;

fn main() {}
