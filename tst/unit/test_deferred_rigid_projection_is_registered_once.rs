/* `merge_by_new(self, other, f)` returns `MergeBy<I::IntoIter, J::IntoIter, F>` for the
   declared `MergeBy<Self, J::IntoIter, F>`: relating `<J as IntoIterator>::IntoIter` with
   `<?J as IntoIterator>::IntoIter` while `?J` is open is deferred.  The rule that
   relates them is re-applied every pass; registering the same deferral again each time
   counted as progress, so the pass never settled and the argument binding `?J = J`
   that resolves it never ran ("spare rules left" on itertools' `merge_by`). */
struct MergeBy<I, J, F>(I, J, F);

fn merge_by_new<I, J, F>(a: I, b: J, f: F) -> MergeBy<I::IntoIter, J::IntoIter, F>
where
    I: IntoIterator,
    J: IntoIterator<Item = I::Item>,
    F: FnMut(&I::Item, &J::Item) -> bool,
{
    MergeBy(a.into_iter(), b.into_iter(), f)
}

trait Tools: Iterator {
    fn merge_by<J, F>(self, other: J, is_first: F) -> MergeBy<Self, J::IntoIter, F>
    where
        Self: Sized,
        J: IntoIterator<Item = Self::Item>,
        F: FnMut(&Self::Item, &Self::Item) -> bool,
    {
        merge_by_new(self, other, is_first)
    }
}

impl<T: Iterator> Tools for T {}

fn main() {
    let m = (0..3).merge_by(vec![5, 6], |a, b| a <= b);
    assert_eq!(m.0.count() + m.1.count(), 5);
}
