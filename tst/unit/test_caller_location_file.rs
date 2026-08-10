use std::panic::Location;

const CALLER: &Location<'static> = Location::caller();

#[inline(never)]
fn runtime_caller() -> &'static Location<'static> {
    Location::caller()
}

fn main() {
    assert_eq!(CALLER.file(), file!());
    let _ = runtime_caller();
}
