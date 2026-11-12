fn absent<T: ?Sized + 'static>() -> Option<&'static T> {
    None
}

fn infer_str() -> Option<*const str> {
    let Some(reference) = absent() else { return None; };
    let pointer: *const str = reference;
    Some(pointer)
}

fn infer_slice() -> Option<*const [u16]> {
    let Some(reference) = absent() else { return None; };
    let pointer: *const [u16] = reference;
    Some(pointer)
}

fn main() {
    assert!(infer_str().is_none());
    assert!(infer_slice().is_none());
}
