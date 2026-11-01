//@ compile-variant: -O
const fn alignment<T>(_: &T) -> usize { std::mem::align_of::<T>() }
const ALIGNMENTS: (usize, usize) = {
    let empty = || 17u8;
    let word = 0u64;
    let captured = move || word;
    (alignment(&empty), alignment(&captured))
};
fn main() {
    assert_eq!(ALIGNMENTS, (1, std::mem::align_of::<u64>()));
}
