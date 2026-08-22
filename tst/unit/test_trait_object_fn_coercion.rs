struct Callback {
    call: Box<dyn FnMut() -> usize>,
}

fn main() {
    let call: Box<dyn Fn() -> usize> = Box::new(|| 42);
    let mut callback = Callback { call };
    assert_eq!((callback.call)(), 42);
}
