trait Relay<T> {
    fn tag() -> u8;
}

struct Leaf;
impl Relay<Leaf> for Leaf {
    fn tag() -> u8 { 71 }
}

struct With<'a, T: Relay<T>> {
    value: &'a T,
}

fn main() {
    let leaf: Leaf = Leaf;
    let selected = With { value: if true { &leaf } else { &leaf } };
    assert_eq!(<Leaf as Relay<Leaf>>::tag(), 71);
    assert_eq!(std::mem::size_of_val(selected.value), 0);
}
