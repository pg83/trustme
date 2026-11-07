#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Width(usize);

impl Width {
    const fn next(self) -> usize {
        self.0 + 1
    }
}

struct Buffer<const N: usize, const M: usize = { Width(N).next() }>([u8; M]);

fn main() {
    let first = Buffer::<3>([17; 4]);
    let second = Buffer::<6>([19; 7]);
    assert_eq!(first.0.len(), 4);
    assert_eq!(second.0.len(), 7);
    assert_eq!(first.0[3], 17);
    assert_eq!(second.0[6], 19);
}
