// A variant's discriminant expression may name another variant of the same enum.
// Reading one has to evaluate the discriminants, and evaluating them must not ask
// for that again -- and a cast reads the discriminant at the enum's declared
// representation, not at whatever width the tag ends up in memory.
enum Backward {
    X = 42,
    Y = Backward::X as isize - 3,
}

#[repr(u8)]
enum Narrow {
    A = 200,
    B = Narrow::A as u8 - 100,
}

fn main() {
    assert_eq!(Backward::X as isize, 42);
    assert_eq!(Backward::Y as isize, 39);
    assert_eq!(Narrow::A as u8, 200);
    assert_eq!(Narrow::A as i8, -56);
    assert_eq!(Narrow::B as u8, 100);
}
