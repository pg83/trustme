enum Signed {
    First = -1,
    Second = -2,
}

fn main() {
    assert_eq!(Signed::Second as isize, -2);
}
