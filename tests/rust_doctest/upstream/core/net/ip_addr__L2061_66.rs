// Extracted from library/core/src/net/ip_addr.rs:2061
#![allow(unused)]
#![feature(ip_as_octets)]
fn main() {
    
    use std::net::Ipv6Addr;
    
    assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).as_octets(),
               &[255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
}
