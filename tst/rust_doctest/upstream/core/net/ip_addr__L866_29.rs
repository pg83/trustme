// Extracted from library/core/src/net/ip_addr.rs:866
#![allow(unused)]
#![feature(ip)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(100, 64, 0, 0).is_shared(), true);
    assert_eq!(Ipv4Addr::new(100, 127, 255, 255).is_shared(), true);
    assert_eq!(Ipv4Addr::new(100, 128, 0, 0).is_shared(), false);
}
