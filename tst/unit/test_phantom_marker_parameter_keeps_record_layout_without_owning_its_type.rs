use std::marker::PhantomData;
struct Packet<T> { payload: u32, marker: PhantomData<T> }
#[inline(never)]
fn assemble<T>(payload: u32, marker: PhantomData<T>) -> Packet<T> {
    Packet { payload, marker }
}
fn main() {
    let marker = std::hint::black_box(PhantomData::<String>);
    let packet = assemble(37, marker);
    assert_eq!(packet.payload, 37);
    assert_eq!(std::mem::size_of::<Packet<String>>(), 4);
    assert!(!std::mem::needs_drop::<Packet<String>>());
}
