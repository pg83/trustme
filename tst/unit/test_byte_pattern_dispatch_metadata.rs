//@ aux-build: byte_pattern_dispatch.rs
use byte_pattern_dispatch::{carry, classify, fixed, text};

const BYTE_RESULTS: [u8; 5] = [
    classify(b""), classify(b"a\0\xff"), classify(b"ab"),
    classify(&[0, 7, 9]), classify(b"a"),
];
const ARRAY_RESULTS: [u8; 3] = [fixed(b"a\0\xff"), fixed(b"ab\0"), fixed(b"abc")];
const TEXT_RESULTS: [u8; 4] = [text(""), text("a\0λ"), text("ab"), text("a")];

fn main() {
    assert_eq!(BYTE_RESULTS, [0, 1, 2, 3, 4]);
    assert_eq!(ARRAY_RESULTS, [11, 12, 13]);
    assert_eq!(TEXT_RESULTS, [21, 22, 23, 24]);
    let cases: [(&[u8], u8); 8] = [
        (b"", 0), (b"a\0\xff", 1), (b"ab", 2), (&[0, 7, 9], 3),
        (b"a", 4), (&[0], 4), (&[0, 7], 4), (b"a\0\xfe", 4),
    ];
    for (bytes, expected) in cases {
        assert_eq!(classify(bytes), expected);
        assert_eq!(carry(123u16, bytes), (123, expected));
    }
    for (bytes, expected) in [(b"a\0\xff", 11), (b"ab\0", 12), (b"abc", 13)] {
        assert_eq!(fixed(bytes), expected);
    }
    for (value, expected) in [("", 21), ("a\0λ", 22), ("ab", 23), ("a", 24)] {
        assert_eq!(text(value), expected);
    }
}
