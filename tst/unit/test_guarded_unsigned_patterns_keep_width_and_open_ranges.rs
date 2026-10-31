macro_rules! check {
    ($ty:ty) => {{
        fn classify(value: $ty, allow: bool) -> u8 {
            match value { 3..=8 if allow => 7, 9 if allow => 11, 10.. => 13, _ => 17 }
        }
        assert_eq!(classify(3, true), 7);
        assert_eq!(classify(8, false), 17);
        assert_eq!(classify(9, true), 11);
        assert_eq!(classify(<$ty>::MAX, false), 13);
        assert_eq!(classify(0, true), 17);
    }};
}
fn main() { check!(u8); check!(u16); check!(u32); check!(u64); check!(u128); check!(usize); }
