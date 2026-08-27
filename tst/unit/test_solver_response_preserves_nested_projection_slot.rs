//@ crate-type: lib

use std::mem::MaybeUninit;

// The `Zip` response contains `<I as IntoIterator>::IntoIter` under another
// nominal type. The response must preserve the relation between its `I` and
// the caller's inference variable; replacing it with a fresh existential
// leaves the desugared loop's `Iterator::Item` uninferred.
pub fn clone_into_new<U: Clone>(source: &[U], target: &mut [MaybeUninit<U>]) {
    for (src, dst) in source.iter().zip(target) {
        dst.write(src.clone());
    }
}
