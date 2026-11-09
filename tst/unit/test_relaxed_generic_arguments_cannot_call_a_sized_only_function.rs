//@ compile-fail: Unsized type not valid here

fn sized_only<T>() {}

fn relaxed<T: ?Sized>() {
    sized_only::<T>();
}

fn main() {}
