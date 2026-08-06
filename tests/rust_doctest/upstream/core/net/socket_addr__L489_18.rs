// Extracted from library/core/src/net/socket_addr.rs:489
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV6, Ipv6Addr};
    
    let mut socket = SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1), 8080, 0, 0);
    socket.set_port(4242);
    assert_eq!(socket.port(), 4242);
}
