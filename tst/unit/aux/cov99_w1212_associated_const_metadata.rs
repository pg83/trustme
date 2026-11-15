#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Width<const N: usize>;
pub trait View {
    type Out;
}
pub struct Word<const N: usize>;

impl<const N: usize> View for Word<N>
where
    [(); N + 1]:,
{
    type Out = Width<{ N + 1 }>;
}
