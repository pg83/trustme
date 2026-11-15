fn factory<T, const N: usize>() -> impl Fn() {
    || {
        let discard = |_| ();
        if false {
            discard(factory::<T, N>());
        }
    }
}

fn main() {
    factory::<u8, 3>()();
}
