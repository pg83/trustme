pub struct Buffer<const N: usize>(pub [u8; N]);

impl<const N: usize> Buffer<N> {
    pub fn length(_: [u8; N]) -> usize {
        N
    }
}

pub fn inferred_item() -> impl Fn([u8; 3]) -> usize {
    Buffer::<_>::length
}
