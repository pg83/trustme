// Extracted from library/core/src/net/ip_addr.rs:1836
#![allow(unused)]
#![feature(ip)]
fn main() {

    use std::net::{Ipv6Addr, Ipv6MulticastScope};

    assert_eq!(
        Ipv6Addr::new(0xff0e, 0, 0, 0, 0, 0, 0, 0).multicast_scope(),
        Some(Ipv6MulticastScope::Global)
    );
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).multicast_scope(), None);
}
