//@ compile-fail: error:

macro_rules! marker {
    () => {};
}

const async extern fn unfinished() {
    marker!
