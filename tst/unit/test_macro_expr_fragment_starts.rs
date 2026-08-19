//@ run-pass
// The macro matcher decides what an `expr` fragment may start with. A label
// heads a loop or a block, and both are expressions, so a fragment may begin
// with one -- as may an explicit `FnMut::call_mut`, whose trait names no
// argument tuple for the closure to be compared against.

#![feature(fn_traits)]

macro_rules! expr {
    ($x:expr) => {
        $x
    };
}

fn main() {
    assert_eq!(expr!('outer: loop { break 'outer 7; }), 7);
    assert_eq!(expr!('outer: loop { break 'outer 1; } + 1), 2);
    assert_eq!(expr!('block: { break 'block 3; }), 3);
    assert_eq!(expr!('outer: while false {}), ());
    assert_eq!(expr!(&&1), &&1);
    assert_eq!(expr!(c"cstr".count_bytes()), 4);

    let mut zero = || 0;
    assert_eq!(zero.call_mut(()), 0);
}
