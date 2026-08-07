// Extracted from library/core/src/net/ip_addr.rs:1670
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::Ipv6Addr;

    // The unspecified and loopback addresses are unicast.
    assert_eq!(Ipv6Addr::UNSPECIFIED.is_unicast(), true);
    assert_eq!(Ipv6Addr::LOCALHOST.is_unicast(), true);

    // Any address that is not a multicast address (`ff00::/8`) is unicast.
    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_unicast(), true);
    assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).is_unicast(), false);
}
