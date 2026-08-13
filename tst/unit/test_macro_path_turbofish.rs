struct Wrapper<T>(T);

macro_rules! accept_path {
    ($path:path) => {
        let _: $path;
    };
}

fn main() {
    accept_path!(Wrapper::<u8>);
}
