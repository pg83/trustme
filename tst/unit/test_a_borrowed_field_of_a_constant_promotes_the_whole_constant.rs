// `&CONST.field` promotes the constant's whole value and borrows the field
// of that static (upstream promotes the temporary the projection is taken
// from), so the field keeps the placement the enclosing value gives it.
// The field alone was promoted, as a static of its own type: hashbrown's
// `&ALIGNED_TAGS.tags`, a byte array inside a struct aligned for SIMD
// loads, came out misaligned and its `load_aligned` assertion failed.
#[repr(C, align(4096))]
struct Page {
    header: [u8; 16],
    tail: u8,
}

const PAGE: Page = Page { header: [7; 16], tail: 3 };

fn header() -> &'static [u8; 16] {
    &PAGE.header
}

fn tail() -> &'static u8 {
    &PAGE.tail
}

fn main() {
    assert_eq!(header().as_ptr() as usize % 4096, 0);
    assert_eq!(header()[15], 7);
    assert_eq!(*tail(), 3);
    assert_eq!(tail() as *const u8 as usize % 4096, 16);
}
