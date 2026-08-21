//@ crate-type: lib
//@ compile-flags: -Znext-solver

struct Normalized;
struct Source;

fn from_normalized_projection<T>()
where
    T: Iterator<Item = Normalized>,
    Source: Into<T::Item>,
{
    let _: Source = Source.into();
}
