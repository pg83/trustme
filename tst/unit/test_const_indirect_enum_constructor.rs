//@ crate-type: lib

enum Local<T> {
    Value(T),
}

const VALUE: Local<i32> = (*&Local::Value)(42);

fn main() {
    match VALUE {
        Local::Value(value) => assert_eq!(value, 42),
    }
}
