struct Leaf<const N: usize>;

impl<const N: usize> Leaf<N> {
    fn make() -> Self {
        Self
    }

    fn size(&self) -> usize {
        N
    }
}

trait Accept {}
impl<const N: usize> Accept for Leaf<N> {}

trait Outer<const N: usize> {
    type Output;
}

impl<const N: usize> Outer<N> for ()
where
    Leaf<N>: Accept,
{
    type Output = Leaf<N>;
}

fn main() {
    let leaf: Leaf<7> = <() as Outer<_>>::Output::make();
    assert_eq!(leaf.size(), 7);
}
