// Extracted from library/core/src/net/socket_addr.rs:453
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV6, Ipv6Addr};
    
    let mut socket = SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1), 8080, 0, 0);
    socket.set_ip(Ipv6Addr::new(76, 45, 0, 0, 0, 0, 0, 0));
    assert_eq!(socket.ip(), &Ipv6Addr::new(76, 45, 0, 0, 0, 0, 0, 0));
}
