fn read_ptr<'a>(ptr: *const Option<i32>) -> Option<&'a i32> {
    unsafe { &*ptr }.as_ref()
}

fn main() {
    let value = Some(7);
    assert_eq!(read_ptr(&value), Some(&7));
}
