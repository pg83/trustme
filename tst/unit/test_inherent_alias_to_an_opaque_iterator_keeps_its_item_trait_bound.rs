#![feature(type_alias_impl_trait, inherent_associated_types)]
#![allow(incomplete_features)]

type Hidden = impl Iterator<Item: Copy>;

struct Factory;

impl Factory {
    type Output = Hidden;
}

#[define_opaque(Hidden)]
fn make() -> Factory::Output {
    [7u8, 11, 13].into_iter()
}

fn main() {
    let mut values = make();
    assert!(values.next().is_some());
    assert_eq!(values.count(), 2);
}
