//@ compile-variant: -O
#[repr(C)]
struct Tail<T: ?Sized> { tag: u8, values: T }
#[repr(C, align(8))]
struct Outer<T: ?Sized> { prefix: u32, tail: T }
const EMPTY: &[u32] = &[];
const NUMBERS: &[u16] = &[7, 13, 23];
const ZEROS: &[()] = &[(); 17];
const DATA: Tail<[u16; 3]> = Tail { tag: 5, values: [7, 13, 23] };
const VIEW: &Tail<[u16]> = &DATA;
const NESTED: Outer<Tail<[u16; 3]>> = Outer { prefix: 31, tail: Tail { tag: 5, values: [7, 13, 23] } };
const NESTED_VIEW: &Outer<Tail<[u16]>> = &NESTED;
const SIZES: [usize; 4] = [std::mem::size_of_val(EMPTY), std::mem::size_of_val(NUMBERS), std::mem::size_of_val(ZEROS), std::mem::size_of_val("жук")];
const ALIGNMENTS: [usize; 4] = [std::mem::align_of_val(EMPTY), std::mem::align_of_val(NUMBERS), std::mem::align_of_val(ZEROS), std::mem::align_of_val("жук")];
const LAYOUT: (usize, usize) = (std::mem::size_of_val(VIEW), std::mem::align_of_val(VIEW));
const NESTED_LAYOUT: (usize, usize) = (std::mem::size_of_val(NESTED_VIEW), std::mem::align_of_val(NESTED_VIEW));
fn main() {
    assert_eq!(SIZES, [0, 6, 0, 6]);
    assert_eq!(ALIGNMENTS, [4, 2, 1, 1]);
    assert_eq!(LAYOUT, (8, 2));
    assert_eq!(NESTED_LAYOUT, (16, 8));
    assert_eq!(VIEW.tag, 5);
    assert_eq!(&VIEW.values, NUMBERS);
    assert_eq!(NESTED_VIEW.prefix, 31);
    assert_eq!(&NESTED_VIEW.tail.values, NUMBERS);
}
