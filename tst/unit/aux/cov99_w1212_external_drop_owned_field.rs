#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Owner(pub String);
struct Width<const N: usize>;
trait View {
    type Out;
}
struct Tag<const M: usize>;

impl<const M: usize> View for Tag<M>
where
    [(); M + 1]:,
{
    type Out = Width<{ M + 1 }>;
}

impl<const M: usize, const N: usize> Drop for Owner
where
    Tag<M>: View<Out = Width<N>>,
{
    fn drop(&mut self) {
        std::hint::black_box(N);
    }
}
