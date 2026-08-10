// Extracted from library/core/src/net/ip_addr.rs:926
#![allow(unused)]
#![feature(ip)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(240, 0, 0, 0).is_reserved(), true);
    assert_eq!(Ipv4Addr::new(255, 255, 255, 254).is_reserved(), true);

    assert_eq!(Ipv4Addr::new(239, 255, 255, 255).is_reserved(), false);
    // The broadcast address is not considered as reserved for future use by this implementation
    assert_eq!(Ipv4Addr::new(255, 255, 255, 255).is_reserved(), false);
}
