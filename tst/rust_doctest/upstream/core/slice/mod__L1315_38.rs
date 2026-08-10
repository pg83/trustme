// Extracted from library/core/src/slice/mod.rs:1315
#![allow(unused)]
fn main() {
    let slice: &[char] = &['l', 'o', 'r', 'e', 'm', '!'];
    let chunks: &[[char; 1]] =
        // SAFETY: 1-element chunks never have remainder
        unsafe { slice.as_chunks_unchecked() };
    assert_eq!(chunks, &[['l'], ['o'], ['r'], ['e'], ['m'], ['!']]);
    let chunks: &[[char; 3]] =
        // SAFETY: The slice length (6) is a multiple of 3
        unsafe { slice.as_chunks_unchecked() };
    assert_eq!(chunks, &[['l', 'o', 'r'], ['e', 'm', '!']]);

    // These would be unsound:
    // let chunks: &[[_; 5]] = slice.as_chunks_unchecked() // The slice length is not a multiple of 5
    // let chunks: &[[_; 0]] = slice.as_chunks_unchecked() // Zero-length chunks are never allowed
}
