pub struct Packet(pub [u16; 3]);
pub const END: u16 = 89;
pub fn checksum(value: Packet) -> u16 { value.0.iter().sum() }
