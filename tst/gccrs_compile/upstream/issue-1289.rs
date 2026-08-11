pub unsafe fn pointer_additions(value: &mut i32, count: usize) -> (*mut i32, *const i32) {
    let mutable = value as *mut i32;
    let constant = value as *const i32;
    unsafe { (mutable.add(count), constant.add(count)) }
}
