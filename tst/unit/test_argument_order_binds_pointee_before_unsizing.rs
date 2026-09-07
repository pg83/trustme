// `mem::replace(s, &mut [])` with `s: &mut &'a mut [T]`: upstream coerces the
// arguments in order, and `&mut &mut [T]` into the parameter `&mut T` unifies the
// unknown pointee with the source's own (`coerce_borrowed_pointer`'s first
// autoderef step, `Unsize` into an unknown being no unsizing), so `T` is
// `&mut [T]` before `&mut [_; 0]` unsizes into it.  Binding `T` from the empty
// array first would make the first argument a mismatch.
//
// Same shape as `<[T]>::split_off_first_mut` in core.
fn take_first<'a, T>(s: &mut &'a mut [T]) -> Option<&'a mut T> {
    let Some((first, rem)) = std::mem::replace(s, &mut []).split_first_mut() else {
        return None;
    };
    *s = rem;
    Some(first)
}

fn main() {
    let mut data = [1, 2, 3];
    let mut slice: &mut [i32] = &mut data;
    let first = take_first(&mut slice).unwrap();
    *first = 7;
    assert_eq!(slice, &[2, 3]);
    assert_eq!(data, [7, 2, 3]);
}
