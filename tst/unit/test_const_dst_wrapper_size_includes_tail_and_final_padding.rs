#[repr(C)]
struct Packet<T: ?Sized> { head: u8, tail: T }
#[repr(C, align(16))]
struct Aligned<T: ?Sized> { head: u8, tail: T }
const SLICE: &Packet<[u16]> = &Packet { head: 17, tail: [23, 37, 41] };
static TEXT_BYTES: Packet<[u8; 4]> = Packet { head: 19, tail: *b"rust" };
const TEXT: &Packet<str> = unsafe {
    let bytes: &Packet<[u8]> = &TEXT_BYTES;
    &*(bytes as *const Packet<[u8]> as *const Packet<str>)
};
const ALIGNED: &Aligned<[u16]> = &Aligned { head: 29, tail: [31, 43, 47] };
const VALUES: (usize, usize, usize, usize) = (
    std::mem::size_of_val(SLICE),
    std::mem::size_of_val(TEXT),
    std::mem::size_of_val(ALIGNED),
    std::mem::size_of_val("rust"),
);
fn main() {
    assert_eq!(VALUES, (8, 5, 16, 4));
    assert_eq!((SLICE.head, &SLICE.tail), (17, &[23, 37, 41][..]));
    assert_eq!(&TEXT.tail, "rust");
}
