//@ compile-fail: No macro arm matched

macro_rules! reject_fragments {
    ($whole:stmt) => {};
    (# ! $after_attribute:stmt) => {};
    (# ! let ref = $after_binding:stmt) => {};
    (# ! let ref = do yeet $capture:ty) => {};
}

fn main() {
    reject_fragments!(# ! let ref = do yeet impl use <);
}
