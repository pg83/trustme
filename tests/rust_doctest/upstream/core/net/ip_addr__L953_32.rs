// Extracted from library/core/src/net/ip_addr.rs:953
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    assert_eq!(Ipv4Addr::new(224, 254, 0, 0).is_multicast(), true);
    assert_eq!(Ipv4Addr::new(236, 168, 10, 65).is_multicast(), true);
    assert_eq!(Ipv4Addr::new(172, 16, 10, 65).is_multicast(), false);
}
