type Huge = [(); 18446744073709551615];

fn main() {
    assert_eq!(core::mem::size_of::<Huge>(), 0);
    assert_eq!(core::mem::align_of::<Huge>(), 1);
    assert_eq!(core::mem::size_of::<[Huge; 2]>(), 0);
}
