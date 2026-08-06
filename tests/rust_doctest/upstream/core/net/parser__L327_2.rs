// Extracted from library/core/src/net/parser.rs:327
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {
    
    use std::net::Ipv4Addr;
    
    let localhost = Ipv4Addr::new(127, 0, 0, 1);
    
    assert_eq!(Ipv4Addr::parse_ascii(b"127.0.0.1"), Ok(localhost));
}
