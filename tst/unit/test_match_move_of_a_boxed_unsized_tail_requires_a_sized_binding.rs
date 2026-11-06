//@ compile-fail: error
struct Packet<T: ?Sized> { tag: u8, data: T }
fn main() {
    let boxed: Box<Packet<[u8]>> = Box::new(Packet { tag: 5, data: [17, 19, 23] });
    match *boxed { value => assert_eq!(value.tag, 5) }
}
