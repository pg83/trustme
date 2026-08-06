// Extracted from library/core/src/net/ip_addr.rs:599
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    let addr = Ipv4Addr::BROADCAST;
    assert_eq!(addr, Ipv4Addr::new(255, 255, 255, 255));
}
