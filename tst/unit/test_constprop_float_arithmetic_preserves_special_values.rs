macro_rules! check {
    ($ty:ty) => {{
        let left: $ty = 7.5;
        let right: $ty = 2.0;
        assert_eq!((left + right, left - right, left * right, left / right, left % right), (9.5, 5.5, 15.0, 3.75, 1.5));
        let negative_zero: $ty = -0.0;
        assert!((negative_zero * right).is_sign_negative());
        assert!((negative_zero / right).is_sign_negative());
        assert!((left / 0.0).is_infinite());
        let nan: $ty = <$ty>::NAN;
        assert!((nan + right).is_nan());
        assert!(!(nan == nan) && nan != nan);
        assert!(!(nan < right) && !(nan >= right));
    }};
}

fn main() { check!(f32); check!(f64); }
