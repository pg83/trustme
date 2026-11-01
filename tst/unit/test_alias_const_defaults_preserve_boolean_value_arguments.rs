#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn width(wide: bool) -> usize { if wide { 5 } else { 3 } }
type Buffer<const WIDE: bool = true, const N: usize = { width(WIDE) }> = [u16; N];

fn main() {
    let first: Buffer = [3, 5, 7, 11, 13];
    let second: Buffer<false> = [17, 19, 23];
    let third: Buffer<false, 2> = [29, 31];
    assert_eq!(first[4], 13);
    assert_eq!(second[2], 23);
    assert_eq!(third[1], 31);
}
