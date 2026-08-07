// Extracted from library/core/src/net/ip_addr.rs:523
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    let addr = Ipv4Addr::new(0x12, 0x34, 0x56, 0x78);
    assert_eq!(0x12345678, addr.to_bits());
}
