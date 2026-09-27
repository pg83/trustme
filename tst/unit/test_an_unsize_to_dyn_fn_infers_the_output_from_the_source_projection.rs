// ahash's `hash(&1u8, &hasher)` with `hash_builder: &dyn Fn() -> T`: the only
// thing that fixes the callee's `T` is the unsize of `&F` to `&dyn Fn() -> ?T`,
// whose goal `F: Fn<(), Output = ?T>` is met by the where-clause `F: Fn<()>`.
// `Output` is declared by `FnOnce`, not `Fn`, so that bound does not carry it;
// upstream proves `<F as FnOnce<()>>::Output == ?T` as its own goal, which the
// param env normalizes to `T`. Only impl candidates took that route here; a
// where-clause candidate skipped the item and left `?T` open.
trait Tr {
    type A;
    fn get(&self) -> Self::A;
}

impl Tr for u8 {
    type A = u16;
    fn get(&self) -> u16 {
        u16::from(*self) * 2
    }
}

fn call<T: Default + Into<u64>>(f: &dyn Fn() -> T) -> u64 {
    f().into() + T::default().into()
}

fn through_fn<T: Default + Into<u64>, F: Fn() -> T>(f: F) -> u64 {
    call(&f)
}

fn through_impl<T: Default + Into<u64>>(f: impl Fn() -> T) -> u64 {
    call(&f)
}

fn get<T: Default + Into<u64>>(t: &dyn Tr<A = T>) -> u64 {
    t.get().into()
}

fn through_trait<T: Default + Into<u64>, X: Tr<A = T>>(x: X) -> u64 {
    get(&x)
}

fn main() {
    assert_eq!(through_fn(|| 3u8), 3);
    assert_eq!(through_impl(|| 5u32), 5);
    assert_eq!(through_trait(7u8), 14);
}
