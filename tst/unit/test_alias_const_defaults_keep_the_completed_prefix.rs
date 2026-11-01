#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Buffers<const N: usize = 3, const M: usize = { N + 2 }, const K: usize = { M + 1 }> = ([u8; N], [u8; M], [u8; K]);

fn main() {
    let first: Buffers = ([1; 3], [2; 5], [3; 6]);
    let second: Buffers<4> = ([5; 4], [7; 6], [11; 7]);
    let third: Buffers<2, 8> = ([13; 2], [17; 8], [19; 9]);
    assert_eq!(first.0.len() + first.1.len() + first.2.len(), 14);
    assert_eq!(second.2[6], 11);
    assert_eq!(third.2[8], 19);
}
