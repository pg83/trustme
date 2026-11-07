#![feature(type_alias_impl_trait, inherent_associated_types)]
#![allow(incomplete_features)]

type Hidden = impl Copy + PartialEq<u16>;
struct Storage<T>(T);
impl Storage<u16> {
    type Row<const N: usize> = [u16; N];
}
trait Identity {
    type Value: ?Sized;
}
impl<T: ?Sized> Identity for T {
    type Value = T;
}

#[define_opaque(Hidden)]
fn produce() -> Hidden {
    let row: <<Storage<Hidden>>::Row<5> as Identity>::Value = [19u16; 5];
    assert_eq!(row.iter().copied().sum::<u16>(), 95);
    47u16
}

fn main() {
    assert!(produce() == 47u16);
}
