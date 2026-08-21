// An `if let` guard may move an outer value.  Each alternative of an or-pattern
// evaluates the guard independently, and the move is committed only on the
// path where that alternative and the guard both match.
#![feature(if_let_guard)]
#![allow(irrefutable_let_patterns)]

fn consume(x: Box<u8>, value: (u8, u8)) -> u8 {
    match value {
        (1, _) | (_, 2) if let y = x => *y,
        _ => *x,
    }
}

fn return_from_guard(x: Box<u8>, value: u8) -> Box<u8> {
    match value {
        0 | 1 if let true = return x => unreachable!(),
        _ => x,
    }
}

fn main() {
    assert_eq!(consume(Box::new(3), (1, 0)), 3);
    assert_eq!(consume(Box::new(5), (0, 2)), 5);
    assert_eq!(consume(Box::new(7), (0, 0)), 7);
    assert_eq!(*return_from_guard(Box::new(11), 0), 11);
    assert_eq!(*return_from_guard(Box::new(13), 2), 13);
}
