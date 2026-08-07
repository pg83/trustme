// Extracted from library/core/src/mem/mod.rs:855
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    use std::mem;

    struct Buffer<T> { buf: Vec<T> }
    impl<T> Buffer<T> {
        fn replace_index(&mut self, i: usize, v: T) -> T {
            mem::replace(&mut self.buf[i], v)
        }
    }

    let mut buffer = Buffer { buf: vec![0, 1] };
    assert_eq!(buffer.buf[0], 0);

    assert_eq!(buffer.replace_index(0, 2), 0);
    assert_eq!(buffer.buf[0], 2);
}
