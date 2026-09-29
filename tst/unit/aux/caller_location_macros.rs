// A `#[track_caller]` method and a macro that calls it the way snapbox's
// `assert_data_eq!` calls `Assert::eq`, for a unit to see what location the
// call reports when the macro is expanded in another crate.
use std::panic::Location;

pub struct Assert;

impl Assert {
    #[track_caller]
    pub fn eq(&self, _value: u8) -> &'static Location<'static> {
        Location::caller()
    }
}

#[macro_export]
macro_rules! remote_here {
    ($value:expr) => {{
        let value = $value;
        $crate::Assert.eq(value)
    }};
}
