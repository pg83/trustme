trait Leaf<const N: usize> {}

impl<F, const N: usize> Leaf<N> for F {}

trait Root<F> {}

impl<F> Root<F> for () where
    F: Leaf<0> + Leaf<1> + Leaf<2> + Leaf<3> + Leaf<4> + Leaf<5> + Leaf<6> + Leaf<7>
{}

fn consume<F>(_: F)
where
    (): Root<F>,
{}

fn main() {
    consume(|| 7u8);
}
