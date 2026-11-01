#![feature(inherent_associated_types)]

struct Block<T>(T);

macro_rules! buffers {
    () => {
        type Buffer<const N: usize> = [Self; N];
        const STEP: usize = 2;
        fn pair(self) -> Self::Buffer<2> {
            [self, self]
        }
    };
}

impl<T: Copy> Block<T> {
    buffers!();
}

impl<T: Copy> Copy for Block<T> {}
impl<T: Copy> Clone for Block<T> {
    fn clone(&self) -> Self { *self }
}

fn main() {
    let pair = Block(19u16).pair();
    assert_eq!(pair[0].0 + pair[1].0, 38);
    assert_eq!(Block::<u16>::STEP, pair.len());
}
