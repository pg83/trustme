//@ native-static-aux-build: own_native_search_value.rs
//@ input: aux/own_native_search_value.rs
//@ input: support/metadata/own_native_search_contract.metadata
//@ input: support/metadata/own_native_search_with_missing_directory.metadata
//@ input: support/metadata/native_search_path.py
//@ compile-flags: --extern contract=$DIR/support/metadata/own_native_search_with_missing_directory.metadata

extern crate contract;

fn main() {
    assert_eq!(unsafe { contract::own_native_search_value() }, 73);
}
