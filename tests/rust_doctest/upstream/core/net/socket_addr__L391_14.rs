// Extracted from library/core/src/net/socket_addr.rs:391
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV4, Ipv4Addr};

    let mut socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);
    socket.set_port(4242);
    assert_eq!(socket.port(), 4242);
}
