fn accept<F: FnOnce() -> u8>(_: F) {}
fn identity<T>(value: T) -> T { value }
fn wrap<T>(value: T) -> Option<T> { Some(value) }
fn main() {
    let first = identity(|| loop {});
    accept(first);
    let second = wrap(|| panic!("not called")).unwrap();
    accept(second);
    let third = *Box::new(|| return panic!("not called"));
    accept(third);
}
