// Extracted from library/core/src/net/ip_addr.rs:462
#![allow(unused)]
#![feature(ip_as_octets)]
fn main() {

    use std::net::{Ipv4Addr, Ipv6Addr, IpAddr};

    assert_eq!(IpAddr::V4(Ipv4Addr::LOCALHOST).as_octets(), &[127, 0, 0, 1]);
    assert_eq!(IpAddr::V6(Ipv6Addr::LOCALHOST).as_octets(),
               &[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1])
}
