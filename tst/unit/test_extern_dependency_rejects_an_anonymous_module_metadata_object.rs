//@ input: support/metadata/own_anon_module.metadata
//@ input: support/metadata/invalid_object_packets.py
//@ input: support/metadata/invalid_dependency_packets.py
//@ compile-flags: --extern own_anon_module=$DIR/support/metadata/own_anon_module.metadata
//@ compile-fail: Expected OpenNamed

extern crate own_anon_module;

fn main() {}
