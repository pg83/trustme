// Extracted from library/core/src/net/socket_addr.rs:205
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, SocketAddr};
    
    let mut socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 8080);
    socket.set_ip(IpAddr::V4(Ipv4Addr::new(10, 10, 0, 1)));
    assert_eq!(socket.ip(), IpAddr::V4(Ipv4Addr::new(10, 10, 0, 1)));
}
