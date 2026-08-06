// Extracted from library/core/src/net/ip_addr.rs:395
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
    
    assert_eq!(IpAddr::V4(Ipv4Addr::new(203, 0, 113, 6)).is_ipv4(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0)).is_ipv4(), false);
}
