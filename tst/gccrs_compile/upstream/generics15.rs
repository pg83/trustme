#![feature(lang_items)]

pub trait A<T> {}

pub struct B<T: A<T>> (T);
