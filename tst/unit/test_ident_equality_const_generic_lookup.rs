struct Buffer<T, const N: usize>([T; N]);

impl<T, const N: usize> Buffer<T, N> {
    fn len(&self) -> usize {
        N
    }
}

fn main() {
    let buffer = Buffer([0_u8; 4]);
    assert_eq!(buffer.len(), 4);
}
