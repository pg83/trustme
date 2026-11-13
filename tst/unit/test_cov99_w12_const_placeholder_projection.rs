struct Leaf<const N: usize>;

impl<const N: usize> Leaf<N> {
    fn make() -> Self {
        Self
    }

    fn size(&self) -> usize {
        N
    }
}

trait Inner<const N: usize> {
    type Output;
}

impl<const N: usize> Inner<N> for () {
    type Output = Leaf<N>;
}

trait Outer<const N: usize> {
    type Output;
}

impl<const N: usize> Outer<N> for () {
    type Output = <() as Inner<N>>::Output;
}

fn main() {
    let leaf: Leaf<7> = <() as Outer<_>>::Output::make();
    assert_eq!(leaf.size(), 7);
}
