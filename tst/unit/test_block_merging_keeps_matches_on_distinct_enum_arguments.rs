//@ compile-variant: -O
#[derive(Clone, Copy)]
enum Choice { First(()), Second(()), Third(()) }
#[inline(never)]
fn select(first: bool, left: Choice, right: Choice) -> u16 {
    if first {
        match left { Choice::First(()) => 13, Choice::Second(()) => 17, Choice::Third(()) => 19 }
    } else {
        match right { Choice::First(()) => 13, Choice::Second(()) => 17, Choice::Third(()) => 19 }
    }
}
fn main() {
    let cases = [(Choice::First(()), 13), (Choice::Second(()), 17), (Choice::Third(()), 19)];
    for &(left, expected_left) in &cases {
        for &(right, expected_right) in &cases {
            assert_eq!(select(true, left, right), expected_left);
            assert_eq!(select(false, left, right), expected_right);
        }
    }
}
