// Extracted from library/core/src/panic/location.rs:106
#![allow(unused)]
fn main() {
    use std::panic::Location;
    
    /// Returns the [`Location`] at which it is called.
    #[track_caller]
    fn get_caller_location() -> &'static Location<'static> {
        Location::caller()
    }
    
    /// Returns a [`Location`] from within this function's definition.
    fn get_just_one_location() -> &'static Location<'static> {
        get_caller_location()
    }
    
    let fixed_location = get_just_one_location();
    assert_eq!(fixed_location.file(), file!());
    assert_eq!(fixed_location.line(), 14);
    assert_eq!(fixed_location.column(), 5);
    
    // running the same untracked function in a different location gives us the same result
    let second_fixed_location = get_just_one_location();
    assert_eq!(fixed_location.file(), second_fixed_location.file());
    assert_eq!(fixed_location.line(), second_fixed_location.line());
    assert_eq!(fixed_location.column(), second_fixed_location.column());
    
    let this_location = get_caller_location();
    assert_eq!(this_location.file(), file!());
    assert_eq!(this_location.line(), 28);
    assert_eq!(this_location.column(), 21);
    
    // running the tracked function in a different location produces a different value
    let another_location = get_caller_location();
    assert_eq!(this_location.file(), another_location.file());
    assert_ne!(this_location.line(), another_location.line());
    assert_ne!(this_location.column(), another_location.column());
}
