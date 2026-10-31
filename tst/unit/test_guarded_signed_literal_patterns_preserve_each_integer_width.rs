macro_rules! check {
    ($ty:ty) => {{
        fn classify(value: $ty, allow: bool) -> u8 {
            match value { <$ty>::MIN if allow => 7, -37 if allow => 11, _ => 13 }
        }
        assert_eq!(classify(<$ty>::MIN, true), 7);
        assert_eq!(classify(-37, true), 11);
        assert_eq!(classify(-37, false), 13);
        assert_eq!(classify(37, true), 13);
    }};
}
fn main() { check!(i8); check!(i16); check!(i32); check!(i64); check!(i128); check!(isize); }
