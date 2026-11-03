//@ compile-variant: -O
struct Parcel { entries: ([u16; 4], u16) }
static PARCEL: Parcel = Parcel { entries: ([521, 523, 541, 547], 3) };
#[inline(never)]
fn read(index: usize) -> u16 {
    let tuple = &PARCEL.entries;
    let slice: &[u16] = &tuple.0;
    slice[index] + tuple.1
}
fn main() {
    assert_eq!(read(std::hint::black_box(0)), 524);
    assert_eq!(read(std::hint::black_box(2)), 544);
    assert_eq!(PARCEL.entries.0[3], 547);
}
