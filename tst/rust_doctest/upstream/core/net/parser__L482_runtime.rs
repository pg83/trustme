// Extracted from library/core/src/net/parser.rs:482
#![allow(unused)]
fn main() {
    use std::net::IpAddr;
    let _foo: IpAddr = "127.0.0.1:8080".parse().expect("Cannot handle the socket port");
}
