// Extracted from library/core/src/net/ip_addr.rs:892
#![allow(unused)]
#![feature(ip)]
fn main() {
    use std::net::Ipv4Addr;
    
    assert_eq!(Ipv4Addr::new(198, 17, 255, 255).is_benchmarking(), false);
    assert_eq!(Ipv4Addr::new(198, 18, 0, 0).is_benchmarking(), true);
    assert_eq!(Ipv4Addr::new(198, 19, 255, 255).is_benchmarking(), true);
    assert_eq!(Ipv4Addr::new(198, 20, 0, 0).is_benchmarking(), false);
}
