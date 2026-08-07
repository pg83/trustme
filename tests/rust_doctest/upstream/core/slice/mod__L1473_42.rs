// Extracted from library/core/src/slice/mod.rs:1473
#![allow(unused)]
fn main() {
    let slice: &mut [char] = &mut ['l', 'o', 'r', 'e', 'm', '!'];
    let chunks: &mut [[char; 1]] =
        // SAFETY: 1-element chunks never have remainder
        unsafe { slice.as_chunks_unchecked_mut() };
    chunks[0] = ['L'];
    assert_eq!(chunks, &[['L'], ['o'], ['r'], ['e'], ['m'], ['!']]);
    let chunks: &mut [[char; 3]] =
        // SAFETY: The slice length (6) is a multiple of 3
        unsafe { slice.as_chunks_unchecked_mut() };
    chunks[1] = ['a', 'x', '?'];
    assert_eq!(slice, &['L', 'o', 'r', 'a', 'x', '?']);

    // These would be unsound:
    // let chunks: &[[_; 5]] = slice.as_chunks_unchecked_mut() // The slice length is not a multiple of 5
    // let chunks: &[[_; 0]] = slice.as_chunks_unchecked_mut() // Zero-length chunks are never allowed
}
