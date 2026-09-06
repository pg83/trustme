//@ run-pass
// `use_ref(&mut &&&&&t)`: the argument's parameter `&T` is still `&?T` when
// the argument is related, and the call returns nothing that could fix it.
// Upstream finds `&&&&&&Box<T>: Unsize<?T>` ambiguous, takes that as "no
// unsizing", and the reborrow unifies `?T` with the first dereference of the
// source (`coerce_borrowed_pointer`); nothing is left to wait for.
fn use_ref<T>(_: &T) {}

fn use_nested<T>(t: &Box<T>) {
    use_ref(&mut &&&&&t);
    use_ref(&&&mut &&&t);
}

// With an expected result the parameter is decided by it first: `T` here
// comes from the return type, and `&mut RefMut<T>` then reaches `&mut T` by
// dereferencing rather than by fixing `T` to `RefMut<T>`.
fn swap_in<T>(cell: &std::cell::RefCell<T>, value: T) -> T {
    std::mem::replace(&mut cell.borrow_mut(), value)
}

fn main() {
    use_nested(&Box::new(1u8));
    let cell = std::cell::RefCell::new(3u8);
    assert_eq!(swap_in(&cell, 4), 3);
    assert_eq!(*cell.borrow(), 4);
}
