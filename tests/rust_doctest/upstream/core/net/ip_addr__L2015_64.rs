// Extracted from library/core/src/net/ip_addr.rs:2015
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;
    
    assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).octets(),
               [0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
}
