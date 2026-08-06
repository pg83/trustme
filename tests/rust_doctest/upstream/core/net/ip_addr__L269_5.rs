// Extracted from library/core/src/net/ip_addr.rs:269
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
    
    assert_eq!(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)).is_loopback(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0x1)).is_loopback(), true);
}
