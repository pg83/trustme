// A pointer to an array casts to a slice pointer: the length comes from the
// array type, not from the source value.
use std::ptr::NonNull;

pub const fn dangling_slice<T>() -> NonNull<[T]> {
    NonNull::<[T; 1]>::dangling()
}

const C: NonNull<[i32]> = dangling_slice();

fn main() {
    assert_eq!(C.as_ptr().len(), 1);

    let array = [1i32, 2, 3];
    let thin: *const [i32; 3] = &array;
    let fat: *const [i32] = thin as *const [i32];
    assert_eq!(fat.len(), 3);
    assert_eq!(unsafe { (&(*fat))[2] }, 3);
}
