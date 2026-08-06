// Extracted from library/core/src/net/ip_addr.rs:1774
#![allow(unused)]
#![feature(ip)]
fn main() {
    
    use std::net::Ipv6Addr;
    
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc613, 0x0).is_benchmarking(), false);
    assert_eq!(Ipv6Addr::new(0x2001, 0x2, 0, 0, 0, 0, 0, 0).is_benchmarking(), true);
}
