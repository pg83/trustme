// Extracted from library/core/src/net/socket_addr.rs:162
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, SocketAddr};
    
    let socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 8080);
    assert_eq!(socket.ip(), IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)));
    assert_eq!(socket.port(), 8080);
}
