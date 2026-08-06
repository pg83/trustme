// Extracted from library/core/src/net/socket_addr.rs:320
#![allow(unused)]
fn main() {
    use std::net::{SocketAddrV4, Ipv4Addr};
    
    let socket = SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 8080);
}
