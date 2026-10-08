// `impl Trait` in the bounds written on a function's generic parameters is
// universal, as in its arguments: rustc's `lower_generics` lowers those
// bounds in the function's `ImplTraitContext::Universal` (a where-clause
// predicate is lowered with `impl Trait` disallowed). So the
// `impl ExactSizeIterator<..>` in
// `I: IntoIterator<IntoIter = impl ExactSizeIterator<Item = &'a H>>` becomes
// another, unnamed, type parameter of the function. fastbloom's builders
// take their items this way.
use std::hash::Hash;

struct Builder(usize);

impl Builder {
    fn items<'a, H: Hash + 'a, I: IntoIterator<IntoIter = impl ExactSizeIterator<Item = &'a H>>>(self, items: I) -> usize {
        self.0 + items.into_iter().len()
    }
}

fn count<'a, H: 'a, I: IntoIterator<IntoIter = impl ExactSizeIterator<Item = &'a H>>>(items: I) -> usize {
    items.into_iter().len()
}

fn main() {
    assert_eq!(count(&[1, 2, 3]), 3);
    assert_eq!(Builder(1).items([1, 2, 3].iter()), 4);
}
