// `Flatten<I>: Iterator` requires `I::Item: IntoIterator<IntoIter = U,
// Item = U::Item>`. rustc proves these as separate projection obligations: the
// one naming `U` before `U` is known stays ambiguous and is retried. We walk a
// bound's bindings in the order of their names' intern ids, which follows the
// crate's source (clap_builder mentions `Item` before `IntoIter`); taking
// `Item = U::Item` first bound the element type to the unnormalised
// `<_ as Iterator>::Item`, and `for pos in v.into_iter().flatten()` could not
// infer the element type of `v`. The `struct Item` below interns `Item` first.
struct Item;

#[derive(Clone, Debug, PartialEq)]
struct Styled(String);

fn positionals(n: usize) -> Vec<Styled> {
    let mut required = Vec::new();
    required.resize(n, None);
    let mut ret_val = Vec::new();
    for pos in required.into_iter().flatten() {
        ret_val.push(pos);
    }
    ret_val
}

fn main() {
    let _ = Item;
    assert_eq!(positionals(3), Vec::<Styled>::new());
}
