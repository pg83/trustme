//@ compile-only

fn deferred_pointer<T: ?Sized>() -> *const T {
    loop {}
}

fn infer_str() -> *const str {
    deferred_pointer()
}

fn infer_slice() -> *const [u16] {
    deferred_pointer()
}

fn main() {}
