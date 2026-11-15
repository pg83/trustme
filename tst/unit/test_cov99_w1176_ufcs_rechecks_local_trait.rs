//@ compile-fail: UFCS trait was not a trait

fn rejected<T>() {
    <dyn T as T>::missing();
}

fn main() {}
