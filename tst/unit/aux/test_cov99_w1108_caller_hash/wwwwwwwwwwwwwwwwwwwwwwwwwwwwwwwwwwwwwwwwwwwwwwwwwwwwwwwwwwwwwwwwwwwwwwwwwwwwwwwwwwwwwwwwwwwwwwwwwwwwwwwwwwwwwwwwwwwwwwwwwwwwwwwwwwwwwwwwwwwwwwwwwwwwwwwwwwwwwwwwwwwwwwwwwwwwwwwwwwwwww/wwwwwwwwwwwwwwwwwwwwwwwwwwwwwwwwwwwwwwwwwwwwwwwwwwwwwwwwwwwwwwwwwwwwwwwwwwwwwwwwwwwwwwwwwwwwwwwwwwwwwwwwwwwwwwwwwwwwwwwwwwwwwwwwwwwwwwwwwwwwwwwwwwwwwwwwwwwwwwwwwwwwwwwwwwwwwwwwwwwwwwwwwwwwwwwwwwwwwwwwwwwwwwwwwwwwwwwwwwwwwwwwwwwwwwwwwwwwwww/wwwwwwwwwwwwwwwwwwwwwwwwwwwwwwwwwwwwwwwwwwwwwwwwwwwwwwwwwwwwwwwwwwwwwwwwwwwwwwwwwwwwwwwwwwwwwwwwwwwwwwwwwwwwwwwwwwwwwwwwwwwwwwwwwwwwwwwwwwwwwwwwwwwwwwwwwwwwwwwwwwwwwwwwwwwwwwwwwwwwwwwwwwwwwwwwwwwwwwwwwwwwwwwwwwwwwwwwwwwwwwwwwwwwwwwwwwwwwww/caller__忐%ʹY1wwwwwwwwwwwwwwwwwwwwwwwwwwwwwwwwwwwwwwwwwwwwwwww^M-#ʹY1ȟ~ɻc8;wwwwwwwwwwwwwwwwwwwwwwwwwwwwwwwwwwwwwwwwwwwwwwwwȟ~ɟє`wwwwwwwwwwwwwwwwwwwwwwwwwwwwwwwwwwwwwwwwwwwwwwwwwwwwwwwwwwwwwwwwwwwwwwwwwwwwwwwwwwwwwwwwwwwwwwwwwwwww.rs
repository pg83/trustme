#[track_caller]
#[inline(never)]
pub fn location() -> &'static std::panic::Location<'static> {
    std::panic::Location::caller()
}
