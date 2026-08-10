// Extracted from library/core/src/net/socket_addr.rs:71
#![allow(unused)]
fn main() {
    use std::net::{Ipv4Addr, SocketAddrV4};

    let socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);

    assert_eq!("127.0.0.1:8080".parse(), Ok(socket));
    assert_eq!(socket.ip(), &Ipv4Addr::new(127, 0, 0, 1));
    assert_eq!(socket.port(), 8080);
}
