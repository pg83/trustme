#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count<T>() -> (usize, u8) where [(); std::mem::size_of::<T>()]: {
    let bytes = [7u8; std::mem::size_of::<T>()];
    (bytes.len(), if bytes.len() == 0 { 0 } else { bytes[0] })
}
struct Block<T>(T);
impl<T> Block<T> {
    const fn count() -> (usize, u8) where [(); std::mem::size_of::<Self>()]: {
        let bytes = [11u8; std::mem::size_of::<Self>()];
        (bytes.len(), if bytes.len() == 0 { 0 } else { bytes[0] })
    }
}
const fn direct<const N: usize>() -> (usize, usize) {
    let bytes = [3u8; N];
    let units = [(); N];
    (bytes.len(), units.len())
}
const FIRST: (usize, u8) = count::<u16>();
const EMPTY: (usize, u8) = count::<()>();
const SECOND: (usize, u8) = Block::<[u32; 3]>::count();
const COUNTS: ((usize, usize), (usize, usize)) = (direct::<0>(), direct::<5>());
const fn metadata(bytes: &[u8], text: &str) -> (usize, usize) { (bytes.len(), text.len()) }
const METADATA: (usize, usize) = metadata(&[3, 5, 7], "λx");
fn main() {
    assert_eq!((FIRST, EMPTY, SECOND), ((2, 7), (0, 0), (12, 11)));
    assert_eq!(COUNTS, ((0, 0), (5, 5)));
    assert_eq!(METADATA, (3, 3));
}
