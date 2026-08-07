// Extracted from library/core/src/net/ip_addr.rs:62
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    let localhost = Ipv4Addr::new(127, 0, 0, 1);
    assert_eq!("127.0.0.1".parse(), Ok(localhost));
    assert_eq!(localhost.is_loopback(), true);
    assert!("012.004.002.000".parse::<Ipv4Addr>().is_err()); // all octets are in octal
    assert!("0000000.0.0.0".parse::<Ipv4Addr>().is_err()); // first octet is a zero in octal
    assert!("0xcb.0x0.0x71.0x00".parse::<Ipv4Addr>().is_err()); // all octets are in hex
}
