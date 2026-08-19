// A pattern that does not match every value cannot stand where an irrefutable
// one is required; what this checks is that the ones that do still work.
struct Wrap(u8);

fn takes(Wrap(n): Wrap) -> u8 {
    n
}

fn main() {
    let Wrap(x) = Wrap(3);
    assert_eq!(x, 3);
    assert_eq!(takes(Wrap(4)), 4);

    // A refutable pattern is fine with an `else`.
    let Some(y) = Some(5u8) else { unreachable!() };
    assert_eq!(y, 5);

    let v = 7u8;
    match v {
        0.. => (),
    }
}
