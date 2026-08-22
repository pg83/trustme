trait Supertrait<T> {}

impl<T> Supertrait<T> for () {}

trait Identity {
    type Output;
}

impl<T> Identity for T {
    type Output = T;
}

trait Middle<T>: Supertrait<()> + Supertrait<T> {
    fn value(&self) -> usize {
        42
    }
}

impl<T> Middle<T> for () {}

trait Leaf: Middle<<() as Identity>::Output> {}

impl Leaf for () {}

fn main() {
    let leaf: &dyn Leaf = &();
    let middle: &dyn Middle<()> = leaf;
    assert_eq!(middle.value(), 42);
}
