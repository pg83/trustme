macro_rules! check {
    ($ty:ty) => {{
        let left: $ty = 29;
        let right: $ty = 3;
        assert_eq!((left + right, left - right, left * right), (32, 26, 87));
        assert_eq!((left / right, left % right), (9, 2));
        assert_eq!((left & right, left | right, left ^ right), (1, 31, 30));
        assert!(left > right && left >= right && right < left && right <= left);
        assert!(left != right && left == 29);
        assert_eq!((left << 2u8, left >> 2i16), (116, 7));
        let top = <$ty>::MAX;
        assert_eq!(top.wrapping_add(1), 0);
        assert_eq!(top ^ top, 0);
    }};
}

fn main() {
    check!(u8); check!(u16); check!(u32);
    check!(u64); check!(u128); check!(usize);
}
