macro_rules! construct {
    ($path:path) => {
        $path(1)
    };
}

struct Wrapper<T>(T);

fn main() {
    let Wrapper(value) = construct!(Wrapper::<u8>);
    assert_eq!(value, 1);
}
