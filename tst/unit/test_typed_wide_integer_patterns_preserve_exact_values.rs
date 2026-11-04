//@ compile-variant: -O
macro_rules! classify {
    ($name:ident, $ty:ty, $first:pat, $second:pat) => {
        #[inline(never)]
        fn $name(value: $ty) -> u8 {
            match value { $first => 17, $second => 23, _ => 29 }
        }
    };
}

classify!(signed, i64, -7i64, 11i64);
classify!(unsigned, u64, 5u64, 0x8000_0000_0000_0005u64);
classify!(word, isize, -13isize, 17isize);

fn main() {
    assert_eq!(signed(std::hint::black_box(-7)), 17);
    assert_eq!(signed(std::hint::black_box(11)), 23);
    for value in [i64::MIN, -8, 0, i64::MAX] {
        assert_eq!(signed(std::hint::black_box(value)), 29);
    }
    assert_eq!(unsigned(std::hint::black_box(5)), 17);
    assert_eq!(unsigned(std::hint::black_box(0x8000_0000_0000_0005)), 23);
    for value in [0, 6, 0x8000_0000_0000_0004, u64::MAX] {
        assert_eq!(unsigned(std::hint::black_box(value)), 29);
    }
    assert_eq!(word(std::hint::black_box(-13)), 17);
    assert_eq!(word(std::hint::black_box(17)), 23);
    for value in [isize::MIN, -12, 0, isize::MAX] {
        assert_eq!(word(std::hint::black_box(value)), 29);
    }
}
