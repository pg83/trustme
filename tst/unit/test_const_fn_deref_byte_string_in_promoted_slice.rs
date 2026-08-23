// Regression: the const evaluator must preserve byte-string backing storage
// when a promoted slice calls a const fn taking `&[u8; N]`.

#[derive(Clone, Copy)]
struct Tag([u8; 4]);

impl Tag {
    const fn new(bytes: &[u8; 4]) -> Self {
        Self(*bytes)
    }
}

const TAGS: &[(&[u8; 4], Tag)] = &[
    (b"Beng", Tag::new(b"bng2")),
    (b"Deva", Tag::new(b"dev2")),
];

fn main() {
    assert_eq!(TAGS[0].0, b"Beng");
    assert_eq!(TAGS[0].1.0, *b"bng2");
    assert_eq!(TAGS[1].0, b"Deva");
    assert_eq!(TAGS[1].1.0, *b"dev2");
}
