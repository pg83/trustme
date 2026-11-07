const fn empty_text() -> &'static str {
    let bytes = std::ptr::slice_from_raw_parts(1usize as *const u8, 0);
    unsafe { &*(bytes as *const str) }
}

#[repr(C)]
struct TextTail<T: ?Sized> {
    prefix: [u8; 0],
    text: T,
}

const fn empty_struct_tail() -> &'static str {
    let bytes = std::ptr::slice_from_raw_parts(1usize as *const u8, 0);
    let text = bytes as *const str;
    let tail = text as *const TextTail<str>;
    unsafe { &(*tail).text }
}

const fn after_only_byte(bytes: &'static [u8; 1]) -> &'static [u8] {
    let [_, tail @ ..] = bytes;
    tail
}

const TEXT: &str = empty_text();
const TAIL_TEXT: &str = empty_struct_tail();
const BYTES: &[u8] = after_only_byte(&[211]);

fn main() {
    assert_eq!(TEXT, "");
    assert_eq!(TAIL_TEXT, "");
    assert_eq!(BYTES, &[]);
    assert_eq!(TEXT.as_ptr() as usize, 1);
    assert_eq!(TAIL_TEXT.as_ptr() as usize, 1);
}
