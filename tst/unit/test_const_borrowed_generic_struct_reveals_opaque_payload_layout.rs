#[repr(C)]
struct Envelope<T> {
    tag: u8,
    payload: T,
}

const fn hidden() -> impl Copy + Into<u64> {
    613u64
}

const WIDTH: usize = std::mem::size_of_val(&Envelope {
    tag: 7,
    payload: hidden(),
});

const ALIGN: usize = std::mem::align_of_val(&Envelope {
    tag: 11,
    payload: hidden(),
});

fn main() {
    assert_eq!(WIDTH, 16);
    assert_eq!(ALIGN, 8);
    let value = Envelope { tag: 19, payload: hidden() };
    assert_eq!(value.tag, 19);
    assert_eq!(value.payload.into(), 613);
}
