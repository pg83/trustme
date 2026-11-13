fn suspended<T: ?Sized + 'static>() -> &'static T {
    loop {}
}

fn string_pointer() -> *const str {
    let view = suspended();
    view
}

fn main() {
    let factory: fn() -> *const str = string_pointer;
    assert_ne!(factory as usize, 0);
}
