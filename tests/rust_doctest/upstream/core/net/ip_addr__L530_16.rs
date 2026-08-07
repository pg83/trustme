// Extracted from library/core/src/net/ip_addr.rs:530
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    let addr = Ipv4Addr::new(0x12, 0x34, 0x56, 0x78);
    let addr_bits = addr.to_bits() & 0xffffff00;
    assert_eq!(Ipv4Addr::new(0x12, 0x34, 0x56, 0x00), Ipv4Addr::from_bits(addr_bits));
}
