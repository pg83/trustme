use std::panic::Location;

trait Tracked {
    #[track_caller]
    fn location(&self) -> &'static Location<'static> {
        Location::caller()
    }
}

impl Tracked for () {}

trait ImplTracked {
    fn location(&self) -> &'static Location<'static>;
}

impl ImplTracked for () {
    #[track_caller]
    fn location(&self) -> &'static Location<'static> {
        Location::caller()
    }
}

fn main() {
    let tracked: &dyn Tracked = &();
    let expected = line!() + 1;
    let location = tracked.location();
    assert_eq!(location.line(), expected);

    let impl_tracked: &dyn ImplTracked = &();
    let location = impl_tracked.location();
    assert_eq!(location.line(), 18);
    assert_eq!(location.column(), 5);
}
