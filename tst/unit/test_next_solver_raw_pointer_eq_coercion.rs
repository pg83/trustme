//@ check-pass
//@ compile-flags: -Znext-solver

fn pointers_equal<T>(left: *const T, right: *mut T) -> bool {
    left == right
}

fn main() {
    let mut value = 0_u8;
    assert!(pointers_equal(&value, &mut value));
}
