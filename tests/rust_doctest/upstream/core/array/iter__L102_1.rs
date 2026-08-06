// Extracted from library/core/src/array/iter.rs:102
#![allow(unused)]
#![feature(array_into_iter_constructors)]
#![feature(maybe_uninit_uninit_array_transpose)]
fn main() {
    use std::array::IntoIter;
    use std::mem::MaybeUninit;
    
    // Hi!  Thanks for reading the code. This is restricted to `Copy` because
    // otherwise it could leak. A fully-general version this would need a drop
    // guard to handle panics from the iterator, but this works for an example.
    fn next_chunk<T: Copy, const N: usize>(
        it: &mut impl Iterator<Item = T>,
    ) -> Result<[T; N], IntoIter<T, N>> {
        let mut buffer = [const { MaybeUninit::uninit() }; N];
        let mut i = 0;
        while i < N {
            match it.next() {
                Some(x) => {
                    buffer[i].write(x);
                    i += 1;
                }
                None => {
                    // SAFETY: We've initialized the first `i` items
                    unsafe {
                        return Err(IntoIter::new_unchecked(buffer, 0..i));
                    }
                }
            }
        }
    
        // SAFETY: We've initialized all N items
        unsafe { Ok(buffer.transpose().assume_init()) }
    }
    
    let r: [_; 4] = next_chunk(&mut (10..16)).unwrap();
    assert_eq!(r, [10, 11, 12, 13]);
    let r: IntoIter<_, 40> = next_chunk(&mut (10..16)).unwrap_err();
    assert_eq!(r.collect::<Vec<_>>(), vec![10, 11, 12, 13, 14, 15]);
}
