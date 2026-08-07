use std::panic::Location;

const CALLER: &Location<'static> = Location::caller();

fn main() {
    assert_eq!(CALLER.file(), file!());
}
