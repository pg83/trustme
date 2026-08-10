use std::iter::once;

fn multiples(value: usize) -> impl Iterator<Item = usize> {
    (0..10).map(move |factor| value * factor)
}

fn main() {
    assert_eq!(once(42).flat_map(multiples.clone()).sum::<usize>(), multiples(42).sum());
}
