#![feature(type_alias_impl_trait, inherent_associated_types)]
#![allow(incomplete_features)]

type Hidden = impl Copy + PartialEq<u16>;
struct Storage<T>(T);
impl<T> Storage<T> {
    type Row<const N: usize> = [T; N];
}
trait Identity {
    type Value: ?Sized;
}
impl<T: ?Sized> Identity for T {
    type Value = T;
}

fn main() {
    let row: <<Storage<Hidden>>::Row<3> as Identity>::Value = [z_make(); 3];
    assert_eq!(row.len(), 3);
    assert!(row.iter().all(|item| *item == 37u16));
}

#[define_opaque(Hidden)]
fn z_make() -> Hidden { 37u16 }
