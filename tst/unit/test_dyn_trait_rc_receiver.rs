use std::rc::Rc;

trait Value {
    fn value(self: Rc<Self>) -> u32;
}

impl Value for u32 {
    fn value(self: Rc<Self>) -> u32 {
        *self
    }
}

fn main() {
    let value: Rc<dyn Value> = Rc::new(42);
    assert_eq!(Value::value(value), 42);

    let value: Rc<dyn Value> = Rc::new(7);
    assert_eq!(Some(value).map(Value::value), Some(7));
}
