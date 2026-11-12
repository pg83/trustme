#![feature(return_type_notation)]

trait Package {
    type Wrapped;
}

impl<T> Package for T {
    type Wrapped = Option<T>;
}

trait Source {
    fn iter() -> impl Iterator<Item = u16>;
}

struct Values;

impl Source for Values {
    fn iter() -> impl Iterator<Item = u16> {
        [23u16, 37, 53].into_iter()
    }
}

fn sum<S>() -> u16
where
    S: Source<iter(..): Package<Wrapped: IntoIterator>>,
{
    S::iter().sum()
}

fn main() {
    assert_eq!(sum::<Values>(), 113);
}
