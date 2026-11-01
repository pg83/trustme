type Buffers<const N: usize = 3, const M: usize = N, const K: usize = M> = ([u8; N], [u8; M], [u8; K]);

fn main() {
    let first: Buffers = ([3; 3], [5; 3], [7; 3]);
    let second: Buffers<5> = ([11; 5], [13; 5], [17; 5]);
    let third: Buffers<7, 2> = ([19; 7], [23; 2], [29; 2]);
    assert_eq!(first.2[2], 7);
    assert_eq!(second.2[4], 17);
    assert_eq!(third.2[1], 29);
}
