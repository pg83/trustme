// Extracted from library/core/src/net/ip_addr.rs:1422
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;
    
    let addr = Ipv6Addr::UNSPECIFIED;
    assert_eq!(addr, Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0));
}
