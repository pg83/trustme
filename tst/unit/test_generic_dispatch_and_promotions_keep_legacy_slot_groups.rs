trait Read<V, const N: usize> {
    type Output;
    fn read(&self, extra: V) -> Self::Output;
    fn keep(self) -> Self where Self: Sized {
        self
    }
}

impl<T: Copy + Into<u64> + 'static, const N: usize> Read<u8, N> for (T, [T; N]) {
    type Output = u64;
    fn read(&self, extra: u8) -> u64 {
        let identity: &'static fn(T) -> T = &(core::convert::identity::<T> as fn(T) -> T);
        let width: &'static usize = &N;
        identity(self.0).into() + *width as u64 + extra as u64
    }
}

fn dynamic<const N: usize>(value: &dyn Read<u8, N, Output = u64>) -> u64 {
    value.read(5)
}

fn deferred<T: Copy + Into<u64> + 'static, const N: usize>(
    value: (T, [T; N]),
) -> impl FnOnce() -> (u64, usize) {
    let identity: &'static fn(T) -> T = &(core::convert::identity::<T> as fn(T) -> T);
    let width: &'static usize = &N;
    move || (identity(value.0).into() + N as u64, *width)
}

fn main() {
    let pair = (7u16, [11u16; 3]);
    let pair = <(u16, [u16; 3]) as Read<u8, 3>>::keep(pair);
    assert_eq!(dynamic::<3>(&pair), 15);
    assert_eq!(deferred::<u16, 3>(pair)(), (10, 3));
}
