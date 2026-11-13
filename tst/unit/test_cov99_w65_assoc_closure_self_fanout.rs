trait Leaf<const N: usize> {}

impl<F, const N: usize> Leaf<N> for F {}

trait Root {}

impl<F> Root for F where
    F: Leaf<0> + Leaf<1> + Leaf<2> + Leaf<3> + Leaf<4> + Leaf<5> + Leaf<6> + Leaf<7>
{}

fn consume<F: Root>(_: F) {}

fn main() {
    consume(|| 7u8);
}
