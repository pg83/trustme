#![feature(trivial_bounds)]
pub trait Action { fn act(&self) -> u32; }
pub struct Missing;
struct Present(u32);
impl Action for Present { fn act(&self) -> u32 { self.0 + 7 } }
pub fn available<T: Clone>(value: T) -> (T, u32) {
    (value.clone(), <Present as Action>::act(&Present(37)))
}
pub fn deferred<T>(value: &Missing) -> u32 where Missing: Action {
    <Missing as Action>::act(value)
}
