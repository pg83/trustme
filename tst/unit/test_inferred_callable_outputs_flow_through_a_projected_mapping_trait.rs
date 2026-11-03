trait Mapping<T> { type Output; fn apply(&mut self, value: T) -> Self::Output; }
impl<T, F, U> Mapping<T> for F where F: FnMut(T) -> U {
    type Output = U;
    fn apply(&mut self, value: T) -> U { self(value) }
}
fn mapped<T, F: Mapping<T>>(values: Vec<T>, mut callback: F) -> impl Iterator<Item = F::Output> {
    values.into_iter().map(move |value| callback.apply(value))
}
fn main() {
    let result = mapped(vec![(7u16, 11u16), (13, 17)], |(a, b)| u32::from(a) + u32::from(b));
    assert_eq!(result.collect::<Vec<_>>(), [18, 30]);
}
