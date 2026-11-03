//@ compile-variant: -O

macro_rules! check_width {
    ($ty:ty) => {{
        let low = <$ty>::MIN;
        let high = <$ty>::MAX;
        assert_eq!(!low, high);
        assert_eq!(!high, low);
        let negative: $ty = -1;
        let zero: $ty = 0;
        assert_eq!(!negative, zero);
        assert_eq!(!zero, negative);
    }};
}

fn main() {
    check_width!(i8);
    check_width!(i16);
    check_width!(i32);
    check_width!(i64);
    check_width!(i128);
    check_width!(isize);
}
