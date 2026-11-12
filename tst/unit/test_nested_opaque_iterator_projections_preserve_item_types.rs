#![feature(impl_trait_in_assoc_type)]

trait Source {
    type Iter: Iterator<Item = u16>;
    fn iter() -> Self::Iter;
}

struct Values;

impl Source for Values {
    type Iter = impl Iterator<Item = u16>;

    fn iter() -> Self::Iter {
        [19u16, 31, 47].into_iter()
    }
}

type Item = <<Values as Source>::Iter as Iterator>::Item;

fn relay(value: Item) -> Item {
    value
}

fn main() {
    let mut values = Values::iter();
    assert_eq!(values.next().map(relay), Some(19));
    assert_eq!(values.next().map(relay), Some(31));
    assert_eq!(values.next().map(relay), Some(47));
    assert_eq!(values.next(), None);
}
