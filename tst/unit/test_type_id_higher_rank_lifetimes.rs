use std::any::TypeId;

trait Two<'a, 'b> {}

fn main() {
    assert_ne!(
        TypeId::of::<fn(&'static ())>(),
        TypeId::of::<for<'a> fn(&'a ())>(),
    );

    assert_eq!(
        TypeId::of::<for<'a, 'b> fn(&'a (), &'b ())>(),
        TypeId::of::<for<'a, 'b> fn(&'b (), &'a ())>(),
    );

    assert_ne!(
        TypeId::of::<for<'a> fn(fn(&'a ()) -> &'a ())>(),
        TypeId::of::<fn(for<'a> fn(&'a ()) -> &'a ())>(),
    );

    assert_ne!(
        TypeId::of::<Box<dyn for<'a> Two<'a, 'a>>>(),
        TypeId::of::<Box<dyn for<'a> Two<'a, 'static>>>(),
    );

    // Keep the element type reachable only through Vec, as in libtest. Its
    // generated Drop body lowers slice destruction directly during codegen.
    let callbacks: Vec<Box<dyn FnOnce() + Send>> = Vec::new();
    drop(callbacks);
}
