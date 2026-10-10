// manyhow's autoref specialization: `let wt = &WhatType::new(); if false {
// let wt: Result<syn::Ident, _> = wt.identify(); }` fixes `T` before
// `wt.manyhow_parse(..)`, and the impl for `WhatType<T>` (`T: Into<..>`)
// gives way to the one for `&WhatType<T>`. Upstream unifies `identify()`'s
// result into the annotated `Result` as soon as it is checked. We look the
// call up in the same sweep that settles `identify()`, and the coercion
// index the receiver's pause consults predated it: `T` was not yet tied to
// that coercion, nothing paused, and the wrong impl was picked.
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

fn main() {
    let wt = &WhatType::new();
    if false {
        let _x: Result<u32, _> = wt.identify();
    }
    assert_eq!(wt.pick(), "default");
    let ws = &WhatType::new();
    if false {
        let _y: Result<String, _> = ws.identify();
    }
    assert_eq!(ws.pick(), "into");
}
