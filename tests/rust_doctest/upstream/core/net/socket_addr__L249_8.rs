// Extracted from library/core/src/net/socket_addr.rs:249
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, SocketAddr};
    
    let mut socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 8080);
    socket.set_port(1025);
    assert_eq!(socket.port(), 1025);
}
