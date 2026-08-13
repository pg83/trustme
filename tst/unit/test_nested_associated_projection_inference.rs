#![allow(dead_code)]

trait Apply<Argument, Result> {
    type Body<Input: Value<Argument>>: Value<Result>;
}

trait Value<T> {
    const VALUE: T;
}

trait Methods {
    type First: Apply<Input, Middle>;
    type Second: Apply<Middle, Output>;
}

trait Evaluate {
    type Methods: Methods;

    const OUTPUT: Output = {
        <<<Self::Methods as Methods>::Second as Apply<_, _>>::Body<
            <<Self::Methods as Methods>::First as Apply<_, _>>::Body<Initial>,
        > as Value<_>>::VALUE
    };
}

struct Initial;

impl Value<Input> for Initial {
    const VALUE: Input = Input;
}

struct Input;
struct Middle;
struct Output;

fn main() {}
