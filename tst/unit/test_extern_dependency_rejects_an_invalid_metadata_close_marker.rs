//@ input: support/metadata/own_invalid_close.metadata
//@ input: support/metadata/invalid_dependency_packets.py
//@ compile-flags: --extern own_invalid_close=$DIR/support/metadata/own_invalid_close.metadata
//@ compile-fail: Expected CloseObject

extern crate own_invalid_close;

fn main() {}
