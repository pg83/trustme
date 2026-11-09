//@ compile-fail: Type parameters on UFCS enum variant

enum Choice<T> {
    Value(T),
}

fn main() {
    let _ = <Choice<u8>>::Value::<u16>(197u8);
}
