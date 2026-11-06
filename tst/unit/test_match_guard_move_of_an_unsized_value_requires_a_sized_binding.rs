//@ compile-fail: error
#![feature(if_let_guard)]
struct Packet<T: ?Sized> { tag: u8, data: T }
fn main() {
    let boxed: Box<Packet<[u8]>> = Box::new(Packet { tag: 7, data: [29, 31] });
    match () { () if let value = *boxed => assert_eq!(value.tag, 7), _ => {} }
}
