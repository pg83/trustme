//@ run-pass
//@ edition: 2024
// `Repeat(|| panic!())` is an iterator of `!`: the impl `impl<A, F: FnMut() -> A>
// Iter for Repeat<F>` reads `A` off `<F as FnOnce<()>>::Output`, and at
// monomorphisation the closure's own impls settle that at `!`.  Upstream lowers
// `F: FnMut() -> A` to `F: FnMut<()>` plus a projection predicate on `FnOnce`,
// the trait that declares `Output`; the closure's `FnMut` impl alone does not
// carry the item and must not be read as lacking it, and a settled `!` output
// gives `A` that value rather than leaving it open.
struct Repeat<F>(F);

trait Iter {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

impl<A, F: FnMut() -> A> Iter for Repeat<F> {
    type Item = A;
    fn next(&mut self) -> Option<A> {
        Some((self.0)())
    }
}

fn count<I: Iter>(it: &mut I, n: usize) -> usize {
    let mut c = 0;
    for _ in 0..n {
        if it.next().is_some() {
            c += 1;
        }
    }
    c
}

fn main() {
    let mut it = Repeat(|| panic!());
    assert_eq!(count(&mut it, 0), 0);
    let mut it = Repeat(|| 7u8);
    assert_eq!(count(&mut it, 3), 3);
}
