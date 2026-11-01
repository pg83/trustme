macro_rules! check {
    ($ty:ty) => {{
        let left: $ty = -29;
        let right: $ty = 3;
        assert_eq!((left + right, left - right, left * right), (-26, -32, -87));
        assert_eq!((left / right, left % right), (-9, -2));
        assert_eq!((left & right, left | right, left ^ right), (3, -29, -32));
        assert!(left < right && left <= right && right > left && right >= left);
        assert!(left != right && left == -29);
        assert_eq!((left << 2u8, left >> 2i16), (-116, -8));
        let bottom = <$ty>::MIN;
        assert_eq!(bottom.wrapping_sub(1), <$ty>::MAX);
        assert_eq!(!left, 28);
    }};
}

fn main() {
    check!(i8); check!(i16); check!(i32);
    check!(i64); check!(i128); check!(isize);
}
