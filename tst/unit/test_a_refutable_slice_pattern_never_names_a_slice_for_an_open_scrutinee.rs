//@ run-pass
//@ edition: 2024
/* coretests `iter::adapters::array_chunks::test_iterator_array_chunks_infer`
   (`for [a, b, c] in xs.iter().copied().array_chunks() { assert_eq!(a + b + c, 4) }`)
   failed to compile with `cannot infer _ = <_ as Add<_>>::Output`.

   `check_pat_slice` (rustc_hir_typeck/src/pat.rs) gives a scrutinee that is still an
   inference variable the array named by the pattern's own shape - N elements with no
   rest binding is an array of exactly N, `try_resolve_slice_ty_to_array_ty` - and only
   where the pattern is irrefutable, which `pat_is_irrefutable` reads off the pattern's
   `DeclOrigin`: a plain `let`, never a match arm.  A refutable one names nothing and
   leaves the scrutinee to `structurally_resolve_type`, i.e. to whatever rule still owns
   it.  No pattern shape anywhere names a *slice*: even a byte-string pattern becomes
   `&[u8]` only against an expected type already known to be one (`check_pat_lit`).

   A `for` loop desugars to `match Iterator::next(&mut it) { Some(pat) => .. }`, so its
   pattern is a match arm's - refutable.  This checker's fallback pass used to hand such
   a pattern's scrutinee the slice `[_]` instead, guessing before the `Iterator::Item`
   rule that owned the scrutinee had its inputs.  That guess cannot unify with the array
   the rule then produced, so the element type never resolved and the `Add` obligation on
   it was reported as un-inferable.  The literals matter: an operator obligation on the
   element (`-3` is `Neg`, `a + b + c` a chain of `Add`) is what keeps the element type
   open long enough for the fallback pass to run. */

/* The shape of `Iterator::array_chunks`: the item is an array over the inner
   iterator's own item, so the scrutinee is a projection of a projection and is
   decided several passes after the pattern is first seen. */
struct Chunks<I: Iterator, const N: usize>(I);

impl<I: Iterator, const N: usize> Iterator for Chunks<I, N> {
    type Item = [I::Item; N];

    fn next(&mut self) -> Option<Self::Item> {
        let mut items = Vec::with_capacity(N);
        for _ in 0..N {
            items.push(self.0.next()?);
        }
        let mut items = items.into_iter();
        Some(std::array::from_fn(|_| items.next().unwrap()))
    }
}

trait ChunkedIterator: Iterator {
    fn chunks<const N: usize>(self) -> Chunks<Self, N>
    where
        Self: Sized,
    {
        Chunks(self)
    }
}

impl<I: Iterator> ChunkedIterator for I {}

fn main() {
    let xs = [[1, 2, -3]];
    for [a, b, c] in xs.into_iter() {
        assert_eq!(a + b + c, 0);
    }

    let ys = [1, 1, 2, -2, 6, 0, 3, 1];
    let mut chunks = 0;
    for [a, b, c] in ys.into_iter().chunks::<3>() {
        assert_eq!(a + b + c, 4);
        chunks += 1;
    }
    assert_eq!(chunks, 2);

    /* A refutable slice pattern whose scrutinee really is a slice still matches it:
       the shape names no type there, the scrutinee already has one. */
    let zs = vec![1i32, 2, 3];
    match zs.as_slice() {
        [a, b, c] => assert_eq!(a + b + c, 6),
        _ => panic!("expected three elements"),
    }
    match zs.as_slice() {
        [first, rest @ ..] => assert_eq!(*first + rest.len() as i32, 3),
        [] => panic!("expected a non-empty slice"),
    }
}
