use std::panic::Location;

extern "Rust" {
    #[track_caller]
    fn unit_tracked_extern() -> &'static Location<'static>;
}

mod provider {
    use std::panic::Location;

    #[track_caller]
    #[no_mangle]
    fn unit_tracked_extern() -> &'static Location<'static> {
        Location::caller()
    }
}

fn main() {
    let expected = line!() + 1;
    let location = unsafe { unit_tracked_extern() };
    assert_eq!(location.line(), expected);
    assert_eq!(location.column(), 29);
}
