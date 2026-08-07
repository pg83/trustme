// Extracted from library/core/src/net/ip_addr.rs:1811
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::Ipv6Addr;

    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_unicast_global(), false);
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unicast_global(), true);
}
