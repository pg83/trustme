//@ run-pass
// Two promoted borrows of the same value are the same value: the address is
// one place, and library code compares those addresses. A `static` the program
// wrote keeps its own place, whatever it holds.

static WRITTEN_A: [u8; 2] = [b'h', b'i'];
static WRITTEN_B: [u8; 2] = [b'h', b'i'];

fn main() {
    const A: [u8; 2] = [b'h', b'i'];
    const B: &'static [u8; 2] = &A;
    const C: *const u8 = B as *const u8;

    let taken = &A as *const u8;
    assert_eq!(taken, C);

    let one: &'static [u8; 2] = &[b'h', b'i'];
    let two: &'static [u8; 2] = &[b'h', b'i'];
    assert_eq!(one.as_ptr(), two.as_ptr());

    let differs: &'static [u8; 2] = &[b'h', b'o'];
    assert_ne!(one.as_ptr(), differs.as_ptr());

    assert_ne!(
        (&WRITTEN_A) as *const _ as usize,
        (&WRITTEN_B) as *const _ as usize
    );
}
