//@ compile-fail: Couldn't find the field missing_field
//@ compile-flags: --emit=metadata -Cpanic=abort

trait HasItem {
    type Item;
}

fn read_field<T: HasItem>(value: T::Item) {
    let _ = value.missing_field;
}

fn main() {}
