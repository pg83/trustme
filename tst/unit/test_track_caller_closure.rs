//@ edition: 2015

#![feature(closure_track_caller)]
#![feature(stmt_expr_attributes)]

use std::panic::Location;

type Result = (u32, bool, &'static Location<'static>);

fn invoke_dyn(value: &mut dyn FnMut(u32, bool) -> Result) -> Result {
    value(0, false)
}

fn main() {
    let mut tracked = #[track_caller] |value: u32, enabled: bool| (value, enabled, Location::caller());
    let untracked = || ();
    untracked();
    let (value, enabled, location) = tracked(42, true);
    let caller_line = line!() - 1;

    assert_eq!(value, 42);
    assert!(enabled);
    assert_eq!(location.file(), file!());
    assert_eq!(location.line(), caller_line);

    let _ = invoke_dyn(&mut tracked);
}
