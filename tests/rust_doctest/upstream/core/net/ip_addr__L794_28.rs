// Extracted from library/core/src/net/ip_addr.rs:794
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::Ipv4Addr;

    // Most IPv4 addresses are globally reachable:
    assert_eq!(Ipv4Addr::new(80, 9, 12, 3).is_global(), true);

    // However some addresses have been assigned a special meaning
    // that makes them not globally reachable. Some examples are:

    // The unspecified address (`0.0.0.0`)
    assert_eq!(Ipv4Addr::UNSPECIFIED.is_global(), false);

    // Addresses reserved for private use (`10.0.0.0/8`, `172.16.0.0/12`, 192.168.0.0/16)
    assert_eq!(Ipv4Addr::new(10, 254, 0, 0).is_global(), false);
    assert_eq!(Ipv4Addr::new(192, 168, 10, 65).is_global(), false);
    assert_eq!(Ipv4Addr::new(172, 16, 10, 65).is_global(), false);

    // Addresses in the shared address space (`100.64.0.0/10`)
    assert_eq!(Ipv4Addr::new(100, 100, 0, 0).is_global(), false);

    // The loopback addresses (`127.0.0.0/8`)
    assert_eq!(Ipv4Addr::LOCALHOST.is_global(), false);

    // Link-local addresses (`169.254.0.0/16`)
    assert_eq!(Ipv4Addr::new(169, 254, 45, 1).is_global(), false);

    // Addresses reserved for documentation (`192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24`)
    assert_eq!(Ipv4Addr::new(192, 0, 2, 255).is_global(), false);
    assert_eq!(Ipv4Addr::new(198, 51, 100, 65).is_global(), false);
    assert_eq!(Ipv4Addr::new(203, 0, 113, 6).is_global(), false);

    // Addresses reserved for benchmarking (`198.18.0.0/15`)
    assert_eq!(Ipv4Addr::new(198, 18, 0, 0).is_global(), false);

    // Reserved addresses (`240.0.0.0/4`)
    assert_eq!(Ipv4Addr::new(250, 10, 20, 30).is_global(), false);

    // The broadcast address (`255.255.255.255`)
    assert_eq!(Ipv4Addr::BROADCAST.is_global(), false);

    // For a complete overview see the IANA IPv4 Special-Purpose Address Registry.
}
