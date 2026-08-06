// Extracted from library/core/src/net/ip_addr.rs:1290
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;
    
    let addr = Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff);
}
