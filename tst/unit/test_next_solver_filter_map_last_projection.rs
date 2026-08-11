//@ check-pass
//@ compile-flags: -Znext-solver

fn into_item<I>(inner: I) -> Option<I::Item>
where
    I: IntoIterator,
{
    inner.into_iter().next()
}

fn last_inner<I, U>(iter: I) -> Option<U::Item>
where
    I: Iterator,
    I::Item: IntoIterator<IntoIter = U, Item = U::Item>,
    U: Iterator,
{
    iter.filter_map(into_item).last()
}

fn main() {
    assert_eq!(
        last_inner::<_, std::option::IntoIter<u8>>(
            [Some(1), None, Some(2)].into_iter(),
        ),
        Some(2),
    );
}
