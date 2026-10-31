//@ edition: 2024
//@ compile-flags: -Zfmt-debug=none --force-warn dead_code
use std::cell::Cell;
use std::fmt;
struct Probe<'a>(&'a Cell<u8>);
impl fmt::Debug for Probe<'_> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.set(self.0.get() + 1);
        formatter.write_str("visible")
    }
}
#[derive(Debug)]
struct Hidden<'a> { r#gen: Probe<'a> }
struct Explicit<'a, 'b>(&'a Probe<'b>);
impl fmt::Display for Explicit<'_, '_> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(self.0, formatter)
    }
}
fn main() {
    let calls = Cell::new(0);
    assert_eq!(format!("before{:?}after", Hidden { r#gen: Probe(&calls) }), "beforeafter");
    assert_eq!(calls.get(), 0);
    let probe = Probe(&calls);
    assert_eq!(format!("{:?}", probe), "");
    assert_eq!(calls.get(), 0);
    assert_eq!(format!("{}", Explicit(&probe)), "visible");
    assert_eq!(calls.get(), 1);
}
