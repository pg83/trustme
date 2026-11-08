//@ aux-build: encoded_union_const_reader.rs
extern crate encoded_union_const_reader as wire;
const VALUE: u32 = wire::stored();
fn main() {
    assert_eq!(VALUE, 0x35271609);
    assert_eq!(wire::stored(), VALUE);
}
