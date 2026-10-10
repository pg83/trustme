// manyhow's handler macros: `if false { _ = function_handler(n.identify(),
// implementation); }` fixes the `T` of `n = &WhatType::new()` through
// `implementation: impl FunctionMacroHandler<T>` before `n.manyhow_parse(..)`
// picks between the impls for `WhatType<T>` and `&WhatType<T>`. Upstream
// binds `implementation` to its parameter as it checks that call, and the
// bound then selects `T` before the later lookup. Our argument bindings run
// after the sweep of revisits, so the lookup came first and took the impl
// that `T` was going to rule out. A lookup now waits for a binding of an
// earlier call's argument tied to its receiver.
use std::marker::PhantomData;

pub struct WhatType<T>(PhantomData<T>);

impl<T> WhatType<T> {
    pub fn new() -> Self {
        WhatType(PhantomData)
    }
    pub fn identify(&self) -> Result<T, ()> {
        unimplemented!()
    }
}

pub trait Pick<T> {
    fn pick(&self) -> &'static str;
}

impl<T: Into<String>> Pick<T> for WhatType<T> {
    fn pick(&self) -> &'static str {
        "into"
    }
}

impl<T: Default> Pick<T> for &WhatType<T> {
    fn pick(&self) -> &'static str {
        "default"
    }
}

pub trait Handler<Input> {
    fn call(&self, input: Input) -> u32;
}

impl<F: Fn(Input) -> u32, Input> Handler<Input> for F {
    fn call(&self, input: Input) -> u32 {
        self(input)
    }
}

fn handle<Input>(input: Result<Input, ()>, body: impl Handler<Input>) -> u32 {
    body.call(input.ok().unwrap())
}

fn main() {
    let implementation = |x: u32| x + 1;
    let wt = &WhatType::new();
    if false {
        let _ = handle(wt.identify(), implementation);
        unreachable!();
    } else {
        assert_eq!(wt.pick(), "default");
    }
}
