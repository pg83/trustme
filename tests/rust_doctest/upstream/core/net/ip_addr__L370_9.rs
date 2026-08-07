// Extracted from library/core/src/net/ip_addr.rs:370
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

    assert_eq!(IpAddr::V4(Ipv4Addr::new(198, 19, 255, 255)).is_benchmarking(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0x2001, 0x2, 0, 0, 0, 0, 0, 0)).is_benchmarking(), true);
}
