//@ compile-fail: Unsized type not valid here

fn width<T>(_: &T) -> usize {
    std::mem::size_of::<T>()
}

fn relaxed<T: ?Sized>(value: &T) -> usize {
    width(value)
}

fn main() {}
