//@ crate-type: lib

trait Outer {
    type Assoc: Iterator<Item: Iterator>;
}

fn needs_bound<Item, Iter>()
where
    Iter: Iterator<Item = Item>,
    Item: Iterator,
{
}

fn test<T: Outer>() {
    needs_bound::<_, T::Assoc>();
}
