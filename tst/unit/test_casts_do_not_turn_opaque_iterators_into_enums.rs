//@ compile-fail: Type mismatch
enum Tag { First }
fn empty<T>() -> impl Iterator<Item = T> { std::iter::empty() }
fn main() {
    let iterator = empty();
    let _ = iterator as Tag;
}
