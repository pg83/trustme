// Extracted from library/core/src/net/ip_addr.rs:158
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    let localhost = Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1);
    assert_eq!("::1".parse(), Ok(localhost));
    assert_eq!(localhost.is_loopback(), true);
}
