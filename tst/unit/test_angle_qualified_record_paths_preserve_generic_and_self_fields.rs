//@ compile-fail: Unexpected token TOK_BRACE_OPEN, expected TOK_DOUBLE_COLON
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
type Alias<T> = Record<T>;
impl<T> Record<T> {
    fn new(value: T) -> Self { <Self> { value } }
    fn take(self) -> T { let <Self> { value } = self; value }
}
fn make<T>(value: T) -> Alias<T> { <Alias<T>> { value } }
fn main() {
    let <Record<u16>> { value } = <Record<u16>> { value: 739 };
    assert_eq!(value, 739);
    let <Alias<u16>> { value } = make(743u16);
    assert_eq!(value, 743);
    assert_eq!(Record::new(751u16).take(), 751);
}
