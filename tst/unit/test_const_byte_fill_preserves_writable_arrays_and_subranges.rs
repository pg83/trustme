const fn fill_words() -> [u16; 3] {
    let mut words = [0u16; 3];
    unsafe { std::ptr::write_bytes(words.as_mut_ptr(), 0x3c, words.len()); }
    words
}

const fn fill_middle() -> [u8; 4] {
    let mut bytes = [7u8, 11, 13, 17];
    unsafe { std::ptr::write_bytes(bytes.as_mut_ptr().add(1), 0xa5, 2); }
    bytes
}

const WORDS: [u16; 3] = fill_words();
const BYTES: [u8; 4] = fill_middle();

fn main() {
    assert_eq!(WORDS, [0x3c3c; 3]);
    assert_eq!(BYTES, [7, 0xa5, 0xa5, 17]);
}
