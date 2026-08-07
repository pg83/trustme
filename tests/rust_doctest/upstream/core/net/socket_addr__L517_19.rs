// Extracted from library/core/src/net/socket_addr.rs:517
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV6, Ipv6Addr};

    let socket = SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1), 8080, 10, 0);
    assert_eq!(socket.flowinfo(), 10);
}
