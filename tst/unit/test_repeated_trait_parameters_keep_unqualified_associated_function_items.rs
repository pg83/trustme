trait Source { fn make() -> u32; }
struct First;
impl Source for First { fn make() -> u32 { 397 } }
fn same<F: Fn() -> u32>(left: F, right: F) -> u32 { left() + right() }
fn generic<T: Source>() -> u32 { same(T::make, T::make) }
fn main() {
    assert_eq!(generic::<First>(), 794);
    assert_eq!(same(First::make, <First as Source>::make), 794);
}
