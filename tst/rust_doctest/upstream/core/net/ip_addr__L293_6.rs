// Extracted from library/core/src/net/ip_addr.rs:293
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

    assert_eq!(IpAddr::V4(Ipv4Addr::new(80, 9, 12, 3)).is_global(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0x1c9, 0, 0, 0xafc8, 0, 0x1)).is_global(), true);
}
