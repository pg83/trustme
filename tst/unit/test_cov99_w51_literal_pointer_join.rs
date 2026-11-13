//@ crate-type: lib

fn unknown_pointer<T>() -> *const T {
    0 as *const T
}

pub fn literal_pointer_join(flag: bool) {
    let _joined = if flag {
        let pointer = unknown_pointer();
        let _: *const _ = pointer;
        pointer
    } else {
        &0
    };
}
