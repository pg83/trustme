//@ compile-flags: -Zmir-opt-level=0

#[repr(C)]
struct Tail<T: ?Sized> { prefix: u16, contents: T }

#[inline(never)]
fn inspect(value: &Tail<str>) -> (u16, usize, usize, usize) {
    assert_eq!(&value.contents, "rust");
    (value.prefix, value.contents.len(), std::mem::size_of_val(value), std::mem::align_of_val(value))
}

fn main() {
    let storage = Tail { prefix: 42, contents: *b"rust" };
    let bytes: &Tail<[u8]> = &storage;
    let text = unsafe { &*(bytes as *const Tail<[u8]> as *const Tail<str>) };
    assert_eq!(inspect(text), (42, 4, 6, 2));
}
