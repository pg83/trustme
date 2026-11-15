//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir
//@ output-contains: .mir "\x03" =0
//@ output-contains: .mir "\x13" =1

#[repr(u8)]
pub enum Packet {
    Data(u8) = 3,
    Ack(u8) = 19,
}

pub fn packet(value: u8) -> Packet {
    Packet::Data(value)
}
