//@ compile-fail: error

fn convert<T: Into<u16>>(value: T) -> u16 {
    value.into()
}

fn main() {
    let mut values = [17u8, 23].into_iter().filter_map(|_| None);
    let _ = values.next().map(convert);
}
