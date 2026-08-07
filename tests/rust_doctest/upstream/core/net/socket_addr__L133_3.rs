// Extracted from library/core/src/net/socket_addr.rs:133
#![allow(unused)]
fn main() {
    use std::net::{Ipv6Addr, SocketAddrV6};

    let socket = SocketAddrV6::new(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 8080, 0, 0);

    assert_eq!("[2001:db8::1]:8080".parse(), Ok(socket));
    assert_eq!(socket.ip(), &Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1));
    assert_eq!(socket.port(), 8080);

    let mut with_scope = socket.clone();
    with_scope.set_scope_id(3);
    assert_eq!("[2001:db8::1%3]:8080".parse(), Ok(with_scope));
}
