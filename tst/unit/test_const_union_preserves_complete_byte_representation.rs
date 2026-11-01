union Bits { short: u8, bytes: [u8; 8], word: u64 }
const VALUE: Bits = Bits { bytes: [13, 27, 41, 55, 69, 83, 97, 111] };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        assert_eq!(value.bytes, [13, 27, 41, 55, 69, 83, 97, 111]);
        assert_eq!(value.word.to_ne_bytes(), value.bytes);
    }
}
