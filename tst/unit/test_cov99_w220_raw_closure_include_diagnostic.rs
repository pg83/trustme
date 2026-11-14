//@ compile-fail: include! requires a string literal - got |r#type

fn main() {
    let _ = include_str!(|r#type: u8| r#type);
}
