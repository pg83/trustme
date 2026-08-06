// Extracted from library/core/src/net/parser.rs:299
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {
    
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
    
    let localhost_v4 = IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1));
    let localhost_v6 = IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1));
    
    assert_eq!(IpAddr::parse_ascii(b"127.0.0.1"), Ok(localhost_v4));
    assert_eq!(IpAddr::parse_ascii(b"::1"), Ok(localhost_v6));
}
