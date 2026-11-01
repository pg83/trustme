//@ compile-fail: cannot use unsized non-slice type in constant patterns
#[derive(PartialEq, Eq)]
struct Packet<T: ?Sized> { prefix: u8, body: T }
const READY: &Packet<[u8]> = &Packet { prefix: 3, body: [5, 7] };

#[inline(never)]
fn ready(value: &Packet<[u8]>) -> bool {
    matches!(value, READY)
}

fn main() {
    assert!(ready(&Packet { prefix: 3, body: [5, 7] }));
    assert!(!ready(&Packet { prefix: 4, body: [5, 7] }));
    assert!(!ready(&Packet { prefix: 3, body: [5, 8] }));
    assert!(!ready(&Packet { prefix: 3, body: [5, 7, 9] }));
    assert!(!ready(&Packet { prefix: 3, body: [] }));
}
