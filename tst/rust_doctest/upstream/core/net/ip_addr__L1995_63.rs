// Extracted from library/core/src/net/ip_addr.rs:1995
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x1).is_loopback(), false);
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x1).to_canonical().is_loopback(), true);
}
