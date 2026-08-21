macro_rules! accepts_type {
    ($ty:ty) => {};
    (?$path:path) => {
        compile_error!("`?Trait` must match the `ty` fragment");
    };
}

trait Trait {}

accepts_type!(?Trait);

fn main() {}
