pub fn deferred<T>() -> T {
    const { panic!() }
}

fn value<T>() -> usize {
    const { 7 }
}

fn main() {
    assert_eq!(value::<u8>(), 7);
}
