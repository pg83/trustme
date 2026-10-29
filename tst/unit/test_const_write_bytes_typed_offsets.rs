const WORDS: [u16; 5] = {
    let mut words = [0x1234u16; 5];
    unsafe {
        std::ptr::write_bytes(words.as_mut_ptr().add(1), 0x5a, 3);
        std::ptr::write_bytes(words.as_mut_ptr().add(5), 0xff, 0);
    }
    words
};
const ZERO_SIZED: [(); 4] = {
    let mut units = [(); 4];
    unsafe { std::ptr::write_bytes(units.as_mut_ptr(), 0x7f, 4); }
    units
};
fn main() {
    assert_eq!(WORDS, [0x1234, 0x5a5a, 0x5a5a, 0x5a5a, 0x1234]);
    assert_eq!(ZERO_SIZED.len(), 4);
}
