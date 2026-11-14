#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Number<const N: usize>;

pub trait Step<const N: usize> {}

impl<const N: usize> Step<N> for Number<{ N + 1 }> where [(); N + 1]: {}
