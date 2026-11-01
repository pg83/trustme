static DATA: u16 = 257;
const VALUE: &() = unsafe { &*((&DATA as *const u16).cast::<u8>().wrapping_sub(1).cast::<()>()) };
fn main() {
    let actual = std::hint::black_box(VALUE) as *const ();
    let expected = (&DATA as *const u16).cast::<u8>().wrapping_sub(1).cast::<()>();
    assert_eq!(actual, expected);
}
