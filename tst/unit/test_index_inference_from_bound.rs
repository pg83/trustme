use std::ops::Index;

fn first<T>(items: &[T]) -> &T
where
    [T]: Index<usize, Output = T>,
{
    &items[0]
}

fn main() {
    assert_eq!(*first(&[11, 22]), 11);
}
