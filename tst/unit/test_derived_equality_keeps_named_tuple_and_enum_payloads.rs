#[derive(PartialEq, Eq)]
struct Record<T> { tag: u8, payload: T }

#[derive(PartialEq, Eq)]
struct Pair(u16, bool);

#[derive(PartialEq, Eq)]
enum Choice { Empty, Value(u8), Named { enabled: bool } }

fn equal<T: Eq>(left: &T, right: &T) -> bool { left == right }

fn main() {
    let left = Record { tag: 1, payload: [3u8, 5] };
    let right = Record { tag: 1, payload: [3u8, 5] };
    assert!(equal(&left, &right));
    assert!(!equal(&Pair(7, false), &Pair(7, true)));
    assert!(equal(&Choice::Named { enabled: true }, &Choice::Named { enabled: true }));
    assert!(!equal(&Choice::Value(4), &Choice::Empty));
}
