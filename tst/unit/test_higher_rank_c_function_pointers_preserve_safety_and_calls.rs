extern "C" fn safe_value(value: &u16) -> u16 {
    *value + 3
}

unsafe extern "C" fn unsafe_value(value: &u16) -> u16 {
    *value + 5
}

fn main() {
    let safe: for<'a> extern "C" fn(&'a u16) -> u16 = safe_value;
    let unsafe_call: for<'a> unsafe extern "C" fn(&'a u16) -> u16 = unsafe_value;
    assert_eq!(safe(&1031), 1034);
    assert_eq!(unsafe { unsafe_call(&1031) }, 1036);
}
