#![feature(generic_arg_infer)]

struct Pair<const N: usize, const M: usize>([u8; N], [u16; M]);

impl<const N: usize, const M: usize> Pair<N, M> {
    const fn width(&self) -> usize {
        N + M + self.0[0] as usize + self.1[0] as usize
    }
}

struct Measure;

impl Measure {
    const fn extent() -> usize {
        let first: Pair<_, _> = Pair([3, 7], [5, 11, 13]);
        let second: Pair<_, _> = Pair([2, 17, 19], [4, 23]);
        first.width() + first.width() + second.width()
    }
}

type Bytes = [u8; Measure::extent()];

fn main() {
    let bytes: Bytes = [29; 37];
    assert_eq!(bytes.len(), 37);
    assert_eq!(Measure::extent(), 37);
}
