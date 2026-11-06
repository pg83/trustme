//@ aux-build: packed_type_id_metadata.rs
#![feature(const_type_id)]
extern crate packed_type_id_metadata as data;
use std::any::TypeId;

fn main() {
    let packet = data::PACKET;
    let kind = packet.kind;
    let callback = packet.callback;
    assert_eq!(kind, const { TypeId::of::<u16>() });
    assert_ne!(kind, const { TypeId::of::<u32>() });
    assert_eq!(callback(), 41);
    let dispatch = <dyn data::Read as data::Read>::read;
    assert_eq!(dispatch(&41u16), 41);
}
