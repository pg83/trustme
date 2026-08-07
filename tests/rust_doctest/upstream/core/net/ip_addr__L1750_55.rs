// Extracted from library/core/src/net/ip_addr.rs:1750
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::Ipv6Addr;

    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_documentation(), false);
    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_documentation(), true);
    assert_eq!(Ipv6Addr::new(0x3fff, 0, 0, 0, 0, 0, 0, 0).is_documentation(), true);
}
