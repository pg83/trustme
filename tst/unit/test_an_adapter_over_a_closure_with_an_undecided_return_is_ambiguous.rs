// `.flatten()` over `filter_map(|v| object(v))` comes before the closure's
// return is decided: `<FilterMap<I, F> as Iterator>::Item` is the impl's `B`
// that `F: FnMut(..) -> Option<B>` decides, so upstream normalizes it to a
// fresh variable and `?B: IntoIterator` is ambiguous - `Flatten`'s
// `Iterator` impl may apply, and the method probe keeps it. nom's
// examples/json_iterator.rs.
fn object<'a>(v: &'a Vec<(u8, u8)>) -> Option<impl Iterator<Item = (u8, u8)> + 'a> {
    Some(v.iter().copied())
}
fn main() {
    let data = vec![vec![(1u8, 2u8)]];
    let s: Vec<(u8, u8)> = data.iter().filter_map(|v| object(v)).flatten().collect();
    assert_eq!(s, vec![(1, 2)]);
}
