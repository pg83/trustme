// A signed integer sign-extends on its way to a pointer, as it would on its way
// to a wider integer. Constant evaluation copied the source bytes instead,
// which left the high half of the pointer zero.
//
// Same shape as the upstream test issues/issue-43291.rs.
fn subtract(a: i8, b: i8) -> *const () {
    (a - b) as *const ()
}

fn main() {
    // The same value computed at run time and folded at compile time.
    assert_eq!(!0usize as *const (), subtract(0, 1));
    assert_eq!(!0usize as *const (), (0i8 - 1) as *const ());

    assert_eq!((-1i8) as *const (), !0usize as *const ());
    assert_eq!((-1i16) as *const (), !0usize as *const ());
    assert_eq!((-1i32) as *const (), !0usize as *const ());
    assert_eq!((-1i64) as *const (), !0usize as *const ());
    assert_eq!((-1isize) as *const (), !0usize as *const ());

    // A smaller negative value keeps its sign too.
    assert_eq!((-2i8) as *const (), (usize::MAX - 1) as *const ());

    // Unsigned sources still zero-extend.
    assert_eq!(255u8 as *const (), 255usize as *const ());
    assert_eq!(1i8 as *const (), 1usize as *const ());
}
