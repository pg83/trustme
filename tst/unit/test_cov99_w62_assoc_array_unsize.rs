trait FixedBuffer {
    type Buffer;
    fn buffer(&self) -> Self::Buffer;
}

struct Source;

impl FixedBuffer for Source {
    type Buffer = &'static [u8; 1];

    fn buffer(&self) -> Self::Buffer {
        &[7]
    }
}

fn main() {
    let bytes: &[u8] = Source.buffer();
    assert_eq!(bytes, &[7]);
}
