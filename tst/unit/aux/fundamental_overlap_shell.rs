#![feature(fundamental)]

#[fundamental]
pub struct Shell<T>(pub T);

pub trait Remote {}
impl Remote for Shell<u16> {}
