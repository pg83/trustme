const fn arithmetic(left: f64, right: f64) -> [f64; 5] {
    [left + right, left - right, left * right, left / right, left % right]
}
const fn compare(left: f64, right: f64) -> [bool; 6] {
    [left == right, left != right, left < right, left <= right, left > right, left >= right]
}
const VALUES: [f64; 5] = arithmetic(7.5, 2.0);
const LESS: [bool; 6] = compare(2.0, 7.5);
const EQUAL: [bool; 6] = compare(-0.0, 0.0);
const GREATER: [bool; 6] = compare(7.5, 2.0);
const UNORDERED: [bool; 6] = compare(f64::NAN, 2.0);
fn main() {
    assert_eq!(VALUES, [9.5, 5.5, 15.0, 3.75, 1.5]);
    assert_eq!(LESS, [false, true, true, true, false, false]);
    assert_eq!(EQUAL, [true, false, false, true, false, true]);
    assert_eq!(GREATER, [false, true, false, false, true, true]);
    assert_eq!(UNORDERED, [false, true, false, false, false, false]);
}
