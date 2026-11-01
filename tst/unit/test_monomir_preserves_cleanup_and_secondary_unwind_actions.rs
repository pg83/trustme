//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0 -Cpanic=unwind

pub struct Action(pub fn());

impl Drop for Action {
    fn drop(&mut self) { (self.0)(); }
}

pub extern "C" fn boundary(callback: fn(), body: fn()) {
    let action = Action(callback);
    body();
    drop(action);
}

pub fn drop_during_unwind(callback: fn(), body: fn()) {
    let action = Action(callback);
    body();
    drop(action);
}
