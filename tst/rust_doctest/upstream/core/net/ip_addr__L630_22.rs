// Extracted from library/core/src/net/ip_addr.rs:630
#![allow(unused)]
#![feature(ip_from)]
fn main() {
    use std::net::Ipv4Addr;

    let addr = Ipv4Addr::from_octets([13u8, 12u8, 11u8, 10u8]);
    assert_eq!(Ipv4Addr::new(13, 12, 11, 10), addr);
}
