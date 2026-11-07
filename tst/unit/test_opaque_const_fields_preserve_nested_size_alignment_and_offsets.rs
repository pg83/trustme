#![feature(type_alias_impl_trait)]

type Hidden = impl Copy;

#[define_opaque(Hidden)]
const HIDDEN: Hidden = [193u32, 197, 199];

#[repr(C)]
struct Packet {
    tag: u8,
    value: Hidden,
}

const VALUE_SIZE: usize = std::mem::size_of::<Hidden>();
const VALUE_ALIGN: usize = std::mem::align_of::<Hidden>();
const PACKET_SIZE: usize = std::mem::size_of::<Packet>();
const VALUE_OFFSET: usize = std::mem::offset_of!(Packet, value);
const NESTED_SIZE: usize = std::mem::size_of::<([Hidden; 2], u32)>();

fn main() {
    let packet = Packet { tag: 17, value: HIDDEN };
    assert_eq!(VALUE_SIZE, 12);
    assert_eq!(VALUE_ALIGN, 4);
    assert_eq!(PACKET_SIZE, 16);
    assert_eq!(VALUE_OFFSET, 4);
    assert_eq!(NESTED_SIZE, 28);
    assert_eq!(packet.tag, 17);
    assert_eq!(std::mem::size_of_val(&packet.value), 12);
}
