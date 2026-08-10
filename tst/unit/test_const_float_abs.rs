const ABSOLUTE: f64 = (-3.5f64).abs();
const FINITE: bool = (-3.5f64).is_finite();

fn main() {
    assert_eq!(ABSOLUTE, 3.5);
    assert!(FINITE);
}
