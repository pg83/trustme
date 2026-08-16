// `let a @ b = value;` binds twice. The shortcut that avoids a temporary for
// `let x = value;` moved the value once per binding, so the second binding
// read an already-moved operand and the compiler aborted with "destructed
// tagged union used".
//
// Same shapes as rustc's check-pass ui tests
// pattern/bindings-after-at/nested-binding-mode-lint.rs and
// borrowck-pat-by-copy-bindings-in-at.rs.
fn main() {
    let mut is_mut @ not_mut = 42;
    is_mut += 1;
    assert_eq!(is_mut, 43);
    assert_eq!(not_mut, 42);

    let other @ mut also = 7u8;
    also = also.wrapping_add(1);
    assert_eq!(other, 7);
    assert_eq!(also, 8);

    // Three bindings, and a nested one.
    let a @ b @ c = 1i32;
    assert_eq!(a + b + c, 3);
}
