// Extracted from library/core/src/net/ip_addr.rs:976
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    assert_eq!(Ipv4Addr::new(255, 255, 255, 255).is_broadcast(), true);
    assert_eq!(Ipv4Addr::new(236, 168, 10, 65).is_broadcast(), false);
}
