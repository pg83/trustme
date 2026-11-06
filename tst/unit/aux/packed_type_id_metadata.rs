#![feature(const_type_id)]
use std::any::TypeId;

#[derive(Clone, Copy)]
#[repr(C, packed)]
pub struct Packet {
    pub kind: TypeId,
    pub callback: fn() -> u16,
}
pub fn answer() -> u16 { 41 }
pub const PACKET: Packet = Packet {
    kind: const { TypeId::of::<u16>() },
    callback: answer,
};

pub trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }
