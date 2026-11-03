fn plain<'λè̕>(value: &'λè̕ u16) -> &'λè̕ u16 { value }
fn raw<'r#λè̕>(value: &'λè̕ u16) -> &'r#λè̕ u16 { value }
fn main() {
    let value = 433u16;
    assert_eq!(*plain(&value), 433);
    assert_eq!(*raw(&value), 433);
}
