// Extracted from library/core/src/mem/mod.rs:792
#![allow(unused)]
fn main() {
    use std::mem;

    struct Buffer<T> { buf: Vec<T> }
    impl<T> Buffer<T> {
        fn get_and_reset(&mut self) -> Vec<T> {
            mem::take(&mut self.buf)
        }
    }

    let mut buffer = Buffer { buf: vec![0, 1] };
    assert_eq!(buffer.buf.len(), 2);

    assert_eq!(buffer.get_and_reset(), vec![0, 1]);
    assert_eq!(buffer.buf.len(), 0);
}
