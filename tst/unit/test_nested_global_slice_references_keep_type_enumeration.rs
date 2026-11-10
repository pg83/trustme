//@ compile-flags: -O -Zdump-mir

static VALUES: [u32; 3] = [71, 73, 79];
static SLICE: &[u32] = &VALUES;
static INNER: &&[u32] = &SLICE;
static OUTER: &&&[u32] = &INNER;

#[inline(never)]
fn read(index: usize) -> u32 {
    OUTER[index]
}

fn main() {
    assert_eq!(read(std::hint::black_box(0)), 71);
    assert_eq!(read(std::hint::black_box(1)), 73);
    assert_eq!(read(std::hint::black_box(2)), 79);
    assert_eq!(OUTER.len(), 3);
}
