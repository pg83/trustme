#![feature(const_trait_impl, const_cmp)]

const fn classify<const N: usize>(text: &str, bytes: &[u8; N], allow: bool) -> u8 {
    let bytes: &[u8] = bytes;
    match (text, bytes) {
        ("ab" | "abc", b"a\0b") if allow => 1,
        ("αβ", b"abc") => 2,
        ("", b"") => 3,
        (_, b"ab" | b"abc") => 4,
        _ => 9,
    }
}

const RESULTS: [u8; 8] = [
    classify("ab", b"a\0b", true),
    classify("abc", b"a\0b", true),
    classify("ab", b"a\0b", false),
    classify("αβ", b"abc", false),
    classify("", b"", false),
    classify("other", b"ab", true),
    classify("other", b"abc", false),
    classify("αγ", b"a\0c", true),
];

fn main() {
    assert_eq!(RESULTS, [1, 1, 9, 2, 3, 4, 4, 9]);
    assert_eq!(classify("abc", b"a\0b", std::hint::black_box(true)), 1);
    assert_eq!(classify("abc", b"a\0b", std::hint::black_box(false)), 9);
    assert_eq!(classify(std::hint::black_box("αβ"), b"abc", false), 2);
}
