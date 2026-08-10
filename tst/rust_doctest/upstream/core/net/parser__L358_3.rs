// Extracted from library/core/src/net/parser.rs:358
#![allow(unused)]
#![feature(addr_parse_ascii)]
fn main() {

    use std::net::Ipv6Addr;

    let localhost = Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1);

    assert_eq!(Ipv6Addr::parse_ascii(b"::1"), Ok(localhost));
}
