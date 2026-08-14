//@ compile-flags: -Zunleash-the-miri-inside-of-you

const fn double(value: usize) -> usize {
    value * 2
}

const fn call(function: fn(usize) -> usize, value: usize) -> usize {
    function(value)
}

const RESULT: usize = call(double, 21);

type Location = &'static std::panic::Location<'static>;

#[track_caller]
const fn tracked_location() -> Location {
    std::panic::Location::caller()
}

const fn call_tracked(function: fn() -> Location) -> Location {
    function()
}

const TRACKED_LOCATION: Location = call_tracked(tracked_location);

trait TrackedMethod {
    fn location() -> Location;
}

struct Tracked;

impl TrackedMethod for Tracked {
    #[track_caller]
    fn location() -> Location {
        std::panic::Location::caller()
    }
}

const TRACKED_METHOD: fn() -> Location = <Tracked as TrackedMethod>::location;

fn main() {
    assert_eq!(RESULT, 42);
    let runtime_location = call_tracked(tracked_location);
    assert_eq!(runtime_location.file(), TRACKED_LOCATION.file());
    assert_eq!(runtime_location.line(), TRACKED_LOCATION.line());
    assert_eq!(runtime_location.column(), TRACKED_LOCATION.column());
    assert_eq!(TRACKED_METHOD().file(), file!());
}
