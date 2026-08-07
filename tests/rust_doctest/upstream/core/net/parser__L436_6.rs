// Extracted from library/core/src/net/parser.rs:436
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {

    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

    let socket_v4 = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 8080);
    let socket_v6 = SocketAddr::new(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)), 8080);

    assert_eq!(SocketAddr::parse_ascii(b"127.0.0.1:8080"), Ok(socket_v4));
    assert_eq!(SocketAddr::parse_ascii(b"[::1]:8080"), Ok(socket_v6));
}
