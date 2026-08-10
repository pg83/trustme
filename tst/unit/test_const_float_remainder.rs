const EVEN_REMAINDER: f64 = 10.0 % 2.0;
const SIGNED_REMAINDER: f64 = -5.5 % 2.0;

fn main() {
    assert_eq!(EVEN_REMAINDER, 0.0);
    assert_eq!(SIGNED_REMAINDER, -1.5);
}
