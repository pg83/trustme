struct Foo<T>(std::marker::PhantomData<T>);

impl<T> Foo<T> {
    const MAGIC: usize = std::mem::size_of::<T>();
}

fn gccrs_main() -> i32 {
    Foo::<u16>::MAGIC as i32 - 2
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
