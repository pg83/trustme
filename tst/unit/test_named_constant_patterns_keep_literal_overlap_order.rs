struct Code<T>(core::marker::PhantomData<T>);
impl<T> Code<T> {
    const VALUE: u16 = 11;
}

const NUMBER: u16 = Code::<u32>::VALUE;
const NEGATIVE: i16 = -7;
const LETTER: char = 'q';
const BYTES: &[u8] = b"xy";

fn classify(value: (u16, i16, char, &str, &[u8])) -> u8 {
    match value {
        (NUMBER, NEGATIVE, LETTER, "ready", BYTES) => 3,
        (0..=NUMBER, -9..=-1, 'a'..='z', "ready", [b'x', ..]) => 5,
        (_, _, _, "waiting", BYTES) => 7,
        _ => 13,
    }
}

fn main() {
    for (value, expected) in [
        ((11, -7, 'q', "ready", &b"xy"[..]), 3),
        ((11, -7, 'q', "ready", &b"xz"[..]), 5),
        ((10, -8, 'r', "ready", &b"x"[..]), 5),
        ((12, -7, 'q', "ready", &b"xy"[..]), 13),
        ((11, -7, 'q', "waiting", &b"xy"[..]), 7),
        ((11, -7, 'q', "waiting", &b"xz"[..]), 13),
    ] {
        assert_eq!(classify(core::hint::black_box(value)), expected);
    }
}
