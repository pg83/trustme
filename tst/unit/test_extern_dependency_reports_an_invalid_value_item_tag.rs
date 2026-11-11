//@ input: support/metadata/own_bad_value_item.metadata
//@ input: support/metadata/invalid_item_packets.py
//@ compile-flags: --extern own_bad_value_item=$DIR/support/metadata/own_bad_value_item.metadata
//@ compile-fail: Invalid value item tag in crate metadata: 255

extern crate own_bad_value_item;

fn main() {}
