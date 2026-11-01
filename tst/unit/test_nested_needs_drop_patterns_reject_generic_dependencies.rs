//@ compile-fail: constant evaluation cannot depend on generic parameters
use std::marker::PhantomData;

const fn dropping<T>() -> bool { std::mem::needs_drop::<T>() }
struct Query<T>(PhantomData<T>);
impl<T> Query<T> { const DROPS: bool = dropping::<T>(); }

fn matches<T>(value: bool) -> bool {
    match value { Query::<T>::DROPS => true, _ => false }
}

fn main() { let _ = matches::<u32>(false); }
