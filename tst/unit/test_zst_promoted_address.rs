//@ run-pass
// Storage the compiler makes for a promoted borrow of a zero-sized value holds
// nothing, so it needs none: the address is the alignment. A `static` the
// program wrote keeps its own place, whatever its size.

#[repr(align(4))]
struct Aligned;

static WRITTEN: Aligned = Aligned;

fn main() {
    let unit: &'static () = &();
    assert_eq!(unit as *const () as usize, 1);

    let aligned: &'static Aligned = &Aligned;
    assert_eq!(aligned as *const Aligned as usize, 4);

    let empty: &'static [i32] = &[];
    assert_eq!(empty.as_ptr() as usize, 4);
    assert_eq!(empty.as_ptr(), <&[i32]>::default().as_ptr());
    assert_eq!(<Vec<i32>>::new().as_ptr(), <&[i32]>::default().as_ptr());

    assert_ne!(&WRITTEN as *const Aligned as usize, 4);
}
