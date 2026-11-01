union Callback<T: Copy> { value: T, address: usize }
struct Factory<T>(std::marker::PhantomData<T>);
impl<T: Copy> Factory<T> {
    const CALLBACK: Callback<fn(T) -> T> = Callback { value: identity::<T> };
}
fn identity<T>(value: T) -> T { value }

fn main() {
    let short = std::hint::black_box(Factory::<u8>::CALLBACK);
    let wide = std::hint::black_box(Factory::<u64>::CALLBACK);
    unsafe {
        assert_eq!((short.value)(29), 29);
        assert_eq!((wide.value)(0x1234_5678_9abc_def0), 0x1234_5678_9abc_def0);
    }
}
