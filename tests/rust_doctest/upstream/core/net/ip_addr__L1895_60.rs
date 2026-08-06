// Extracted from library/core/src/net/ip_addr.rs:1895
#![allow(unused)]
#![feature(ip)]
fn main() {
    
    use std::net::{Ipv4Addr, Ipv6Addr};
    
    let ipv4_mapped = Ipv4Addr::new(192, 0, 2, 255).to_ipv6_mapped();
    assert_eq!(ipv4_mapped.is_ipv4_mapped(), true);
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc000, 0x2ff).is_ipv4_mapped(), true);
    
    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_ipv4_mapped(), false);
}
