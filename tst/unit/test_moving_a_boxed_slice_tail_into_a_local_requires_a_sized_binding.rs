//@ compile-fail: error
struct Packet<T: ?Sized> { tag: u8, data: T }
fn main() {
    let boxed: Box<Packet<[u8]>> = Box::new(Packet { tag: 3, data: [7, 11, 13] });
    let value = *boxed;
    let index = std::hint::black_box(1usize);
    let selected = value.data[index];
    let [first, ..] = value.data else { return; };
    assert_eq!((value.tag, first, selected), (3, 7, 11));
}
