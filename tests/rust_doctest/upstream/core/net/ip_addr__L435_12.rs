// Extracted from library/core/src/net/ip_addr.rs:435
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
    
    let localhost_v4 = Ipv4Addr::new(127, 0, 0, 1);
    
    assert_eq!(IpAddr::V4(localhost_v4).to_canonical(), localhost_v4);
    assert_eq!(IpAddr::V6(localhost_v4.to_ipv6_mapped()).to_canonical(), localhost_v4);
    assert_eq!(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)).to_canonical().is_loopback(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x1)).is_loopback(), false);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x1)).to_canonical().is_loopback(), true);
}
