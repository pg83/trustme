fn repeated<T: Copy>(value: T) -> (T, T) {
    let captured = move || value;
    let duplicate = captured;
    (captured(), duplicate())
}
fn diverge() -> ! { panic!("uninvoked never-returning function") }
fn main() {
    assert_eq!(repeated(191u32), (191, 191));
    assert_eq!(repeated(()), ((), ()));
    let callback: fn() -> ! = diverge;
    assert_eq!(core::mem::size_of_val(&callback), core::mem::size_of::<usize>());
}
