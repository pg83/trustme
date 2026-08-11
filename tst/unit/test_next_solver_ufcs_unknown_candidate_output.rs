//@ check-pass
//@ compile-flags: -Znext-solver

trait CharCollection {
    fn matches(&mut self, value: char) -> bool;
}

trait AClone: Sized {
    fn aclone(&self) -> Self;
}

impl<T: ?Sized> AClone for &T {
    fn aclone(&self) -> Self {
        *self
    }
}

trait ZFnOnce<A> {
    type Output;
}

trait ZFn<A>: ZFnOnce<A> {}

impl<A, F: ?Sized> ZFn<A> for &F where F: ZFn<A> {}

impl<F> CharCollection for F
where
    F: ZFn<char, Output = bool>,
{
    fn matches(&mut self, _value: char) -> bool {
        false
    }
}

impl<const N: usize> CharCollection for &[char; N] {
    fn matches(&mut self, value: char) -> bool {
        self.contains(&value)
    }
}

trait Searcher<'a> {}

trait Pattern: Sized {
    type Searcher<'a>: Searcher<'a>;

    fn into_searcher(self, text: &str) -> Self::Searcher<'_>;
}

struct CollectionPattern<C: CharCollection>(C);

struct CollectionSearcher<'a, C: CharCollection> {
    chars: C,
    text: &'a str,
}

impl<'a, C: CharCollection> Searcher<'a> for CollectionSearcher<'a, C> {}

impl<'a, C> AClone for CollectionSearcher<'a, C>
where
    C: CharCollection + AClone,
{
    fn aclone(&self) -> Self {
        CollectionSearcher { chars: self.chars.aclone(), text: self.text }
    }
}

impl<C: CharCollection> Pattern for CollectionPattern<C> {
    type Searcher<'a> = CollectionSearcher<'a, C>;

    fn into_searcher(self, text: &str) -> CollectionSearcher<'_, C> {
        CollectionSearcher { chars: self.0, text }
    }
}

struct CharArrayRefSearcher<'a, 'b, const N: usize>(
    <CollectionPattern<&'b [char; N]> as Pattern>::Searcher<'a>,
);

impl<'a, 'b, const N: usize> AClone for CharArrayRefSearcher<'a, 'b, N>
where
    [char; N]: ZFn<char, Output = bool>,
{
    fn aclone(&self) -> Self {
        CharArrayRefSearcher(self.0.aclone())
    }
}

// Keep this impl after the projection user: while Resolve UFCS Outer visits
// the derived impl above, this associated output is still a local
// `UfcsUnknown`, just like core's reference forwarding impls.
impl<A, F: ?Sized> ZFnOnce<A> for &F
where
    F: ZFn<A>,
{
    type Output = F::Output;
}

fn main() {}
