// Extracted from library/core/src/net/ip_addr.rs:1648
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unique_local(), false);
    assert_eq!(Ipv6Addr::new(0xfc02, 0, 0, 0, 0, 0, 0, 0).is_unique_local(), true);
}
