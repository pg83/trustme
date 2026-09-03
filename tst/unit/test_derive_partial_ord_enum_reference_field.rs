// `#[derive(PartialOrd)]` on an enum with a reference field compares that
// field through a further reference. Relating the two sides must not end up
// binding an inference variable to a type that contains that same variable.

#[derive(PartialEq, PartialOrd)]
enum Test<'a> {
    Only(&'a u8),
    Slice(&'a [u8]),
    Empty,
}

fn main() {
    let one = 1u8;
    let two = 2u8;
    let bytes = [3u8, 4];

    assert!(Test::Only(&one) < Test::Only(&two));
    assert!(Test::Only(&two) < Test::Slice(&bytes));
    assert!(Test::Slice(&bytes) < Test::Empty);
    assert!(Test::Only(&one) == Test::Only(&one));
}
