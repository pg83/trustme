// Extracted from library/core/src/net/ip_addr.rs:570
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    let addr = Ipv4Addr::LOCALHOST;
    assert_eq!(addr, Ipv4Addr::new(127, 0, 0, 1));
}
