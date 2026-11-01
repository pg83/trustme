//@ compile-variant: -O
#![feature(const_type_name)]
use std::marker::PhantomData;

const fn name<T>() -> &'static str { std::any::type_name::<u32>() }
const fn dropping<T>() -> bool { std::mem::needs_drop::<u32>() }
struct Query<T>(PhantomData<T>);
impl<T> Query<T> {
    const NAME: &'static str = name::<T>();
    const DROPS: bool = dropping::<T>();
}

fn name_matches<T>(value: &str) -> bool {
    match value { Query::<T>::NAME => true, _ => false }
}
fn drop_matches<T>(value: bool) -> bool {
    match value { Query::<T>::DROPS => true, _ => false }
}

fn main() {
    assert!(name_matches::<String>("u32"));
    assert!(!name_matches::<String>("u64"));
    assert!(name_matches::<u8>("u32"));
    assert!(drop_matches::<String>(false));
    assert!(!drop_matches::<String>(true));
    assert!(drop_matches::<u8>(false));
}
