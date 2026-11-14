//@ compile-fail: Too many type parameters passed to
fn accept<T: ?Sized>() {}

fn probe<T>() {
    accept::<dyn Send<T>>();
}

fn main() {}
