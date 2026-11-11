//@ input: support/metadata/own_wrong_class.metadata
//@ input: support/metadata/invalid_object_packets.py
//@ input: support/metadata/invalid_dependency_packets.py
//@ compile-flags: --extern own_wrong_class=$DIR/support/metadata/own_wrong_class.metadata
//@ compile-fail: Expecting OpenNamed

extern crate own_wrong_class;

fn main() {}
