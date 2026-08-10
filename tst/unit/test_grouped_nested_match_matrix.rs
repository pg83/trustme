#[derive(Clone, Copy)]
enum Leaf {
    Bit(bool),
    Pair(u8, u8),
    Empty,
}

#[derive(Clone, Copy)]
struct Outer {
    tag: u8,
    inner: Leaf,
}

fn classify_outer(value: Outer) -> u8 {
    match value {
        Outer { tag: 0, inner: Leaf::Pair(1, _) } => 10,
        Outer { inner: Leaf::Bit(true), .. } => 20,
        Outer { tag: 0, inner: Leaf::Pair(_, 2) } => 30,
        Outer { tag: 0, .. } => 40,
        Outer { inner: Leaf::Empty, .. } => 50,
        _ => 60,
    }
}

fn classify_pair(value: (Leaf, Leaf)) -> u8 {
    match value {
        (Leaf::Bit(true), Leaf::Pair(1, _)) => 1,
        (Leaf::Bit(_), Leaf::Pair(_, 2)) => 2,
        (_, Leaf::Empty) => 3,
        _ => 4,
    }
}

fn classify_slice(value: &[u8]) -> u8 {
    match value {
        [0, 1, .., 9] => 1,
        [0, .., 9] => 2,
        [0, 1] => 3,
        [_, _] => 4,
        _ => 5,
    }
}

fn main() {
    assert_eq!(classify_outer(Outer { tag: 0, inner: Leaf::Pair(1, 2) }), 10);
    assert_eq!(classify_outer(Outer { tag: 9, inner: Leaf::Bit(true) }), 20);
    assert_eq!(classify_outer(Outer { tag: 0, inner: Leaf::Pair(9, 2) }), 30);
    assert_eq!(classify_outer(Outer { tag: 0, inner: Leaf::Bit(false) }), 40);
    assert_eq!(classify_outer(Outer { tag: 9, inner: Leaf::Empty }), 50);
    assert_eq!(classify_outer(Outer { tag: 9, inner: Leaf::Pair(9, 9) }), 60);

    assert_eq!(classify_pair((Leaf::Bit(true), Leaf::Pair(1, 2))), 1);
    assert_eq!(classify_pair((Leaf::Bit(false), Leaf::Pair(1, 2))), 2);
    assert_eq!(classify_pair((Leaf::Pair(0, 0), Leaf::Empty)), 3);
    assert_eq!(classify_pair((Leaf::Empty, Leaf::Bit(false))), 4);

    assert_eq!(classify_slice(&[0, 1, 7, 9]), 1);
    assert_eq!(classify_slice(&[0, 7, 9]), 2);
    assert_eq!(classify_slice(&[0, 1]), 3);
    assert_eq!(classify_slice(&[8, 9]), 4);
    assert_eq!(classify_slice(&[0, 1, 7]), 5);
}
