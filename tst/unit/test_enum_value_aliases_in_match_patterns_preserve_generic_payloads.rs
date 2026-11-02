//@ aux-build: enum_value_path_exports.rs
//@ compile-variant: -O
use enum_value_path_exports::{Packet, NONE};
fn extract<T: Copy>(value: Packet<T>) -> Option<T> {
    match value { NONE => None, Packet::Data(value) => Some(value), Packet::Zero() => None }
}
fn main() {
    assert_eq!(extract::<u32>(NONE), None);
    assert_eq!(extract(Packet::Data(41u16)), Some(41));
    assert_eq!(extract(Packet::<u8>::Zero()), None);
}
