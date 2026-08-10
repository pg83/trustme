// Extracted from library/core/src/net/parser.rs:410
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {

    use std::net::{Ipv6Addr, SocketAddrV6};

    let socket = SocketAddrV6::new(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 8080, 0, 0);

    assert_eq!(SocketAddrV6::parse_ascii(b"[2001:db8::1]:8080"), Ok(socket));
}
