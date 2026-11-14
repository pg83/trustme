trait Same<T> {}

impl<T> Same<T> for T {}

fn keep<T, F: Same<T>>(f: F) -> F {
    f
}

fn main() {
    let f = keep(|pair: (u8, _)| pair.1);
    assert_eq!(f((3, 17u16)), 17);
}
