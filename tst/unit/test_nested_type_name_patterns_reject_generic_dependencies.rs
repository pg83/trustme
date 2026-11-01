//@ compile-fail: constant evaluation cannot depend on generic parameters
#![feature(const_type_name)]
use std::marker::PhantomData;

const fn name<T>() -> &'static str { std::any::type_name::<T>() }
struct Query<T>(PhantomData<T>);
impl<T> Query<T> { const NAME: &'static str = name::<T>(); }

fn matches<T>(value: &str) -> bool {
    match value { Query::<T>::NAME => true, _ => false }
}

fn main() { let _ = matches::<u32>("u32"); }
