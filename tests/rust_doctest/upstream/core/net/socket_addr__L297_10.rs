// Extracted from library/core/src/net/socket_addr.rs:297
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv6Addr, SocketAddr};
    
    let socket = SocketAddr::new(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 65535, 0, 1)), 8080);
    assert_eq!(socket.is_ipv4(), false);
    assert_eq!(socket.is_ipv6(), true);
}
