//@ aux-build: cov99_w1382_default_body.rs
//@ aux-build: cov99_w1382_z_provider.rs
//@ aux-build: cov99_w1382_a_provider.rs
//@ compile-flags: -Z share-generics=y

extern crate cov99_w1382_z_provider;
extern crate cov99_w1382_a_provider;
extern crate cov99_w1382_default_body;

fn main() {
    assert_eq!(cov99_w1382_z_provider::callback()(17), 17);
    assert_eq!(cov99_w1382_a_provider::callback()(23), 23);
    let selected: fn(u32) -> u32 = <u8 as cov99_w1382_default_body::Callback>::echo;
    assert_eq!(selected(29), 29);
}
