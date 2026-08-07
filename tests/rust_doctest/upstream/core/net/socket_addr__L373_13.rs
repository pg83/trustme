// Extracted from library/core/src/net/socket_addr.rs:373
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV4, Ipv4Addr};

    let socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);
    assert_eq!(socket.port(), 8080);
}
