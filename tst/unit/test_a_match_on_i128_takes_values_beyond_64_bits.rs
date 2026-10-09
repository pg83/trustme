// num-order 1.2: `match *self { i128::MAX | MINP1 => .., i128::MIN => .., u => .. }`.
// A switch over an `i128` has arms whose values do not fit 64 bits.
fn bucket(v: i128) -> i8 {
    const MINP1: i128 = i128::MIN + 1;
    match v {
        i128::MAX | MINP1 => 0,
        i128::MIN => -1,
        -5 => 5,
        7 => 7,
        _ => 9,
    }
}

fn main() {
    assert_eq!(bucket(i128::MAX), 0);
    assert_eq!(bucket(i128::MIN + 1), 0);
    assert_eq!(bucket(i128::MIN), -1);
    assert_eq!(bucket(-5), 5);
    assert_eq!(bucket(7), 7);
    assert_eq!(bucket(1 << 100), 9);
    assert_eq!(bucket(-(1 << 100)), 9);
}
