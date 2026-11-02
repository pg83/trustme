#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

#[inline(never)]
fn read(values: [u16]) -> u16 {
    match values {
        [first, second, ..] => first + second,
        [only] => only,
        [] => 0,
    }
}

fn main() {
    let two: Box<[u16]> = Box::new([13, 17, 19]);
    let one: Box<[u16]> = Box::new([23]);
    let empty: Box<[u16]> = Box::new([]);
    assert_eq!(read(*two), 30);
    assert_eq!(read(*one), 23);
    assert_eq!(read(*empty), 0);
}
