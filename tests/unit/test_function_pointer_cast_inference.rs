enum Value<T> {
    Some(isize, T),
    None((), ((), ()), [i8; 0]),
}

impl<T> Value<T> {
    fn get_ref(&self) -> (isize, &T) {
        match *self {
            Value::None(..) => panic!(),
            Value::Some(number, ref value) => (number, value),
        }
    }
}

fn main() {
    let function = main;
    let value = Value::Some::<fn()>(23, function);
    match value.get_ref() {
        (23, pointer) => assert_eq!(main as fn(), *pointer as fn()),
        _ => panic!(),
    }
}
