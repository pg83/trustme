// Extracted from library/core/src/net/ip_addr.rs:694
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(127, 0, 0, 1).is_loopback(), true);
    assert_eq!(Ipv4Addr::new(45, 22, 13, 197).is_loopback(), false);
}
