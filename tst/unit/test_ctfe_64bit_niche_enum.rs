#[repr(usize)]
#[derive(Copy, Clone)]
enum Level {
    Trace = 0,
    Error = 4,
}

#[repr(transparent)]
#[derive(Copy, Clone)]
struct LevelFilter(Option<Level>);

impl LevelFilter {
    const OFF: Self = Self(None);
}

fn main() {
    assert!(matches!(LevelFilter::OFF.0, None));
    assert_eq!(std::mem::size_of::<LevelFilter>(), std::mem::size_of::<usize>());
}
