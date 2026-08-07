// Extracted from library/core/src/net/socket_addr.rs:355
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV4, Ipv4Addr};

    let mut socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);
    socket.set_ip(Ipv4Addr::new(192, 168, 0, 1));
    assert_eq!(socket.ip(), &Ipv4Addr::new(192, 168, 0, 1));
}
