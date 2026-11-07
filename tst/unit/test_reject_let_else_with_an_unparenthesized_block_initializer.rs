//@ compile-fail: right curly brace
fn main() {
    assert!({
        let Some(value) = { Some(827u16) } else { panic!("missing value") };
        value == 827
    });
}
