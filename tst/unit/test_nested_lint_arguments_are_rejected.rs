//@ compile-fail: error

#[allow(unused_variables(nested))]
fn main() {
    let _value = 3u8;
}
