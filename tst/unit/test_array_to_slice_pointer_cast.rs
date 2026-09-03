// Casting `&[T; N]` to `*const [U]` unsizes on the way, but the cast does not
// decide `T`: upstream lets the element type settle on its own - here the
// integer literals take their own fallback - and only then checks the cast.
// Relating the element to the destination *slice* instead of to its element is
// what tripped the "Setting primitive to [i32]" bug check.

fn main() {
    let array = &[1, 2, 4] as *const [i32];
    assert!(!array.is_null());
    unsafe {
        assert_eq!(*(array as *const i32), 1);
        assert_eq!(*(array as *const i32).add(2), 4);
    }

    let typed: &[u8; 2] = &[5, 6];
    let widened = typed as *const [u8];
    unsafe {
        assert_eq!(*(widened as *const u8).add(1), 6);
    }
}
