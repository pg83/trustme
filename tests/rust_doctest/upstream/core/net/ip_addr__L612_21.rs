// Extracted from library/core/src/net/ip_addr.rs:612
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    let addr = Ipv4Addr::new(127, 0, 0, 1);
    assert_eq!(addr.octets(), [127, 0, 0, 1]);
}
