//@ compile-flags: -Zdump-ast
#![feature(yeet_expr)]

fn produce(fail: bool) -> Result<u16, &'static str> {
    if fail {
        do yeet "cancelled";
    }
    Ok(23)
}

fn main() {
    assert_eq!(produce(false), Ok(23));
    assert_eq!(produce(true), Err("cancelled"));
}
