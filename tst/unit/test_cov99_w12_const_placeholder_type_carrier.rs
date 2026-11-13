struct Leaf<const N: usize>;

impl<const N: usize> Leaf<N> {
    fn make() -> Self {
        Self
    }

    fn size(&self) -> usize {
        N
    }
}

trait Pass {
    type Item;
}

impl<T> Pass for T {
    type Item = T;
}

trait Outer<T> {
    type Output;
}

impl<T, U> Outer<T> for ()
where
    T: Pass<Item = U>,
{
    type Output = U;
}

fn main() {
    let leaf: Leaf<7> = <() as Outer<Leaf<_>>>::Output::make();
    assert_eq!(leaf.size(), 7);
}
