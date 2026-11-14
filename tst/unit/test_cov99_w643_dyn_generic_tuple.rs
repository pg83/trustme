//@ compile-fail: type annotations needed

trait NotDispatchable {
    fn probe<T>(&self);
}

fn call(object: &dyn NotDispatchable) {
    object.probe::<(_,)>()
}

fn main() {}
