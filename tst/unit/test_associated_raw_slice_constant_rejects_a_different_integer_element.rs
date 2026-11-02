//@ compile-fail: Type mismatch between
trait Data { const VALUES: *const [u16]; }
struct Table;
impl Data for Table { const VALUES: *const [u16] = &[61, 67, 71] as *const [u16]; }
fn main() {
    assert_eq!(unsafe { &*<Table as Data>::VALUES }, &[61u16, 67, 71]);
}
