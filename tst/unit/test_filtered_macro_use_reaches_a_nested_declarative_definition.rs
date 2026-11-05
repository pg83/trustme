//@ aux-build: macro_chain_value_origin.rs
//@ aux-build: macro_chain_value_hop.rs
//@ compile-variant: -O
#[macro_use(forwarded_value)]
extern crate macro_chain_value_hop;

fn main() {
    let value: u8 = forwarded_value!();
    assert_eq!(value, 37);
}
