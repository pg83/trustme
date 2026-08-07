// Extracted from library/core/src/slice/iter.rs:324
#![allow(unused)]
#![feature(slice_iter_mut_as_mut_slice)]
fn main() {

    let mut slice: &mut [usize] = &mut [1, 2, 3];

    // First, we get the iterator:
    let mut iter = slice.iter_mut();
    // Then, we get a mutable slice from it:
    let mut_slice = iter.as_mut_slice();
    // So if we check what the `as_mut_slice` method returned, we have "[1, 2, 3]":
    assert_eq!(mut_slice, &mut [1, 2, 3]);

    // We can use it to mutate the slice:
    mut_slice[0] = 4;
    mut_slice[2] = 5;

    // Next, we can move to the second element of the slice, checking that
    // it yields the value we just wrote:
    assert_eq!(iter.next(), Some(&mut 4));
    // Now `as_mut_slice` returns "[2, 5]":
    assert_eq!(iter.as_mut_slice(), &mut [2, 5]);
}
