// Extracted from library/core/src/net/parser.rs:489
#![allow(unused)]
fn main() {
    use std::net::SocketAddr;

    // No problem, the `panic!` message has disappeared.
    let _foo: SocketAddr = "127.0.0.1:8080".parse().expect("unreachable panic");
}
