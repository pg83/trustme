//@ run-pass
/* aes's tests read their vectors out of an `include_bytes!` blob, and every
   vector's slices point into that one allocation. rustc emits an allocation
   once and points into it; the generated C++ wrote the whole allocation out
   again for every pointer into it - 3.8 GB for aes's `tests/mod.rs`. */
const DATA: &[u8] = b"shared allocation marker 0123456789";

pub static PIECES: [&[u8]; 4] = [DATA.split_at(7).0, DATA.split_at(7).1, DATA.split_at(18).1, DATA];

fn main() {
    let total: usize = PIECES.iter().map(|piece| piece.len()).sum();
    assert_eq!(total, 7 + 28 + 17 + 35);
    assert_eq!(PIECES[0], b"shared ");
    assert_eq!(PIECES[1], b"allocation marker 0123456789");
    assert_eq!(PIECES[2], b"marker 0123456789");
    assert_eq!(PIECES[1].as_ptr(), PIECES[3].as_ptr().wrapping_add(7));
}
