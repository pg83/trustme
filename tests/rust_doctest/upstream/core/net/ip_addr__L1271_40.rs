// Extracted from library/core/src/net/ip_addr.rs:1271
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr};
    
    let addr = IpAddr::from([13u8, 12u8, 11u8, 10u8]);
    assert_eq!(IpAddr::V4(Ipv4Addr::new(13, 12, 11, 10)), addr);
}
