// A cast to a pointer at an unsized pointee unsizes what it points at, and that
// unsizing is what names the array's element type when nothing else does: `&mut
// [] as *mut [isize]` writes the element nowhere else.  Leaving the element to
// settle on its own left the cast rule pending and typecheck stopped with it
// still on the list.

static mut EMPTY_RAW: *mut [isize] = &mut [] as *mut _;

fn main() {
    let thin = &[] as *const i32;
    assert!(!thin.is_null());
    let array = &[] as *const [u8; 0];
    assert_eq!(unsafe { (&*array).len() }, 0);
    unsafe {
        let slice: &[isize] = &*EMPTY_RAW;
        assert_eq!(slice.len(), 0);
    }
}
