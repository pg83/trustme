// Extracted from library/core/src/net/ip_addr.rs:1339
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    let addr = Ipv6Addr::new(
        0x1020, 0x3040, 0x5060, 0x7080,
        0x90A0, 0xB0C0, 0xD0E0, 0xF00D,
    );
    assert_eq!(0x102030405060708090A0B0C0D0E0F00D_u128, addr.to_bits());
}
