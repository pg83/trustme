// Extracted from library/core/src/net/ip_addr.rs:1568
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::Ipv6Addr;

    // Most IPv6 addresses are globally reachable:
    assert_eq!(Ipv6Addr::new(0x26, 0, 0x1c9, 0, 0, 0xafc8, 0x10, 0x1).is_global(), true);

    // However some addresses have been assigned a special meaning
    // that makes them not globally reachable. Some examples are:

    // The unspecified address (`::`)
    assert_eq!(Ipv6Addr::UNSPECIFIED.is_global(), false);

    // The loopback address (`::1`)
    assert_eq!(Ipv6Addr::LOCALHOST.is_global(), false);

    // IPv4-mapped addresses (`::ffff:0:0/96`)
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_global(), false);

    // Addresses reserved for benchmarking (`2001:2::/48`)
    assert_eq!(Ipv6Addr::new(0x2001, 2, 0, 0, 0, 0, 0, 1,).is_global(), false);

    // Addresses reserved for documentation (`2001:db8::/32` and `3fff::/20`)
    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1).is_global(), false);
    assert_eq!(Ipv6Addr::new(0x3fff, 0, 0, 0, 0, 0, 0, 0).is_global(), false);

    // Unique local addresses (`fc00::/7`)
    assert_eq!(Ipv6Addr::new(0xfc02, 0, 0, 0, 0, 0, 0, 1).is_global(), false);

    // Unicast addresses with link-local scope (`fe80::/10`)
    assert_eq!(Ipv6Addr::new(0xfe81, 0, 0, 0, 0, 0, 0, 1).is_global(), false);

    // For a complete overview see the IANA IPv6 Special-Purpose Address Registry.
}
