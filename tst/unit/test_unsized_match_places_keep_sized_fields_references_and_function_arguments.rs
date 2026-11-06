#![feature(if_let_guard, unsized_fn_params)]
struct Packet<T: ?Sized> { tag: u8, data: T }
#[inline(never)]
fn consume(packet: Packet<[u8]>) -> (u8, u8) {
    match &packet {
        Packet { tag, data } if let Some(first) = data.first() => (*tag, *first),
        _ => (0, 0),
    }
}
fn main() {
    let boxed: Box<Packet<[u8]>> = Box::new(Packet { tag: 11, data: [37, 41, 43] });
    let tag = match *boxed { ref whole => whole.tag };
    let last = match *boxed { Packet { data: ref bytes, .. } => bytes[2] };
    assert_eq!((tag, last), (11, 43));
    assert_eq!(consume(*boxed), (11, 37));
}
