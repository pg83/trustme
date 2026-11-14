//@ compile-flags: -C opt-level=2 -Z dump-mir

static VALUE: u8 = 37;

#[inline(never)]
fn inspect(seed: u8) -> (usize, (&'static u8, u8)) {
    let pair = (&VALUE, seed);
    let value = *pair.0 as usize;
    (value, pair)
}

fn main() {
    let (value, pair) = inspect(9);
    assert_eq!(value, 37);
    assert_eq!(*pair.0, 37);
    assert_eq!(pair.1, 9);
}
