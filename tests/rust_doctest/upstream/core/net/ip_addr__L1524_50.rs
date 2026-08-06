// Extracted from library/core/src/net/ip_addr.rs:1524
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;
    
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_loopback(), false);
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0x1).is_loopback(), true);
}
