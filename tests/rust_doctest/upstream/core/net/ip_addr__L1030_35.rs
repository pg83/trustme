// Extracted from library/core/src/net/ip_addr.rs:1030
#![allow(unused)]
fn main() {
    use std::net::{Ipv4Addr, Ipv6Addr};
    
    assert_eq!(
        Ipv4Addr::new(192, 0, 2, 255).to_ipv6_compatible(),
        Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0xc000, 0x2ff)
    );
}
