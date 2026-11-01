//@ compile-variant: -O
#[inline(never)]
fn select(first: bool, left: u16, right: u16) -> u16 {
    if first {
        match left { 3 => 13, 7 => 17, 11 => 19, _ => 23 }
    } else {
        match right { 3 => 13, 7 => 17, 11 => 19, _ => 23 }
    }
}
fn main() {
    let cases = [(3, 13), (7, 17), (11, 19), (0, 23), (65535, 23)];
    for &(left, expected_left) in &cases {
        for &(right, expected_right) in &cases {
            assert_eq!(select(true, left, right), expected_left);
            assert_eq!(select(false, left, right), expected_right);
        }
    }
}
