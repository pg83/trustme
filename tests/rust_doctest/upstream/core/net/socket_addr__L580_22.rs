// Extracted from library/core/src/net/socket_addr.rs:580
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV6, Ipv6Addr};
    
    let mut socket = SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1), 8080, 0, 78);
    socket.set_scope_id(42);
    assert_eq!(socket.scope_id(), 42);
}
