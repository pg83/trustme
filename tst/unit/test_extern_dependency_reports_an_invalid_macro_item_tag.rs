//@ input: support/metadata/own_bad_macro_item.metadata
//@ input: support/metadata/invalid_item_packets.py
//@ compile-flags: --extern own_bad_macro_item=$DIR/support/metadata/own_bad_macro_item.metadata
//@ compile-fail: Invalid macro item tag in crate metadata: 255

extern crate own_bad_macro_item;

fn main() {}
