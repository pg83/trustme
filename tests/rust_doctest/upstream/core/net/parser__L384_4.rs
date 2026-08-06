// Extracted from library/core/src/net/parser.rs:384
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {
    
    use std::net::{Ipv4Addr, SocketAddrV4};
    
    let socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);
    
    assert_eq!(SocketAddrV4::parse_ascii(b"127.0.0.1:8080"), Ok(socket));
}
