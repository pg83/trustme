macro_rules! matches_pattern {
    ($value:expr, $pattern:pat) => {
        matches!($value, $pattern)
    };
}

fn main() {
    assert!(!matches_pattern!(-50_i8, -128_i8..=-101_i8));
}
