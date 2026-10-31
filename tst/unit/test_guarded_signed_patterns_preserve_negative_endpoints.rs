macro_rules! check {
    ($ty:ty) => {{
        fn classify(value: $ty, allow: bool) -> u8 {
            match value { ..=-3 if allow => 7, -2..=4 if allow => 11, 5.. => 13, _ => 17 }
        }
        assert_eq!(classify(<$ty>::MIN, true), 7);
        assert_eq!(classify(-3, false), 17);
        assert_eq!(classify(-2, true), 11);
        assert_eq!(classify(4, true), 11);
        assert_eq!(classify(<$ty>::MAX, false), 13);
    }};
}
fn main() { check!(i8); check!(i16); check!(i32); check!(i64); check!(i128); check!(isize); }
