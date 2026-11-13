trait Relay<T> {
    fn tag() -> u8;
}

struct Leaf;

impl Relay<Leaf> for Leaf {
    fn tag() -> u8 { 71 }
}

fn choose<T: Relay<T>>(items: &[&T]) -> u8 {
    assert_eq!(items.len(), 1);
    <T as Relay<T>>::tag()
}

fn main() {
    let leaf = Leaf;
    assert_eq!(choose(&[&leaf]), 71);
}
