macro_rules! check {
    ($ty:ty) => {{
        fn classify(value: $ty, allow: bool) -> u8 {
            match value { -0.0 if allow => 7, -3.5..=2.25 if allow => 11, 2.25.. => 13, _ => 17 }
        }
        assert_eq!(classify(0.0, true), 7);
        assert_eq!(classify(-0.0, false), 17);
        assert_eq!(classify(-3.5, true), 11);
        assert_eq!(classify(2.25, false), 13);
        assert_eq!(classify(<$ty>::INFINITY, false), 13);
        assert_eq!(classify(<$ty>::NAN, true), 17);
    }};
}
fn main() { check!(f32); check!(f64); }
