//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only
//@ output-contains: .mir DSTMETA usize;

#[repr(C)]
pub struct Tail<T: ?Sized> {
    pub value: T,
}

#[repr(C)]
pub struct Packet<T: ?Sized> {
    pub tag: u16,
    pub tail: Tail<T>,
}

pub fn read(packet: Box<Packet<[u32]>>) -> usize {
    packet.tail.value.len() + packet.tag as usize
}
