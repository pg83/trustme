//@ crate-type: lib
#![no_std]
#![crate_name = "own_native_search_contract"]

#[link(name = "own_native_search_value", kind = "static")]
extern "C" {
    pub fn own_native_search_value() -> u16;
}
