//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs fn once
macro_rules! bind { ($name:ident) => { $name }; }
trait Reader { fn read(bind!(value): u8) -> u8; }
struct Identity;
impl Reader for Identity {
    fn read(bind!(value): u8) -> u8 { value }
}
fn once(bind!(value): u8) -> u8 {
    let Some(bind!(inner)) = Some(value) else { return 0; };
    inner
}
fn main() {
    let mut total = 0u8;
    for bind!(value) in [2u8, 3] { total += once(value); }
    let callback = |bind!(value): u8| value + 1;
    let matched = match Some(total) {
        Some(bind!(value)) => callback(value),
        None => 0,
    };
    if let Some(bind!(value)) = Some(matched) {
        assert_eq!(Identity::read(value), 6);
    }
}
