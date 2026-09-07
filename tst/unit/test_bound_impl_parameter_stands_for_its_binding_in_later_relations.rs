/* `Flatten<Map<Iter<i32>, F>>: Iterator` is proven by `impl<I, U> Iterator for
   Flatten<I> where I::Item: IntoIterator<IntoIter = U, Item = U::Item>, U: Iterator`.
   `IntoIter = U` fixes `U = Range<?x>` (`?x` the closure's element, still open), and
   `Item = U::Item` is then related to the nested output.  Upstream `U` is an
   inference variable, so that relation reads `<Range<?x> as Iterator>::Item`; a stale
   `<U as Iterator>::Item` with `U` still the impl's own placeholder exported an
   equality to a variable nothing ever binds, and `it.next()`'s item could not be
   inferred. */
fn main() {
    let xs = [0, 3, 6];
    let mut it = xs.iter().map(|&x| x..x + 3).flatten();
    assert_eq!(it.next(), Some(0));
    assert_eq!(it.next_back(), Some(8));
    let i = it.fold(1, |i, x| {
        assert_eq!(x, i);
        i + 1
    });
    assert_eq!(i, 8);
}
