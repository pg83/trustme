// Extracted from library/core/src/net/ip_addr.rs:552
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    let addr = Ipv4Addr::from_bits(0x12345678);
    assert_eq!(Ipv4Addr::new(0x12, 0x34, 0x56, 0x78), addr);
}
