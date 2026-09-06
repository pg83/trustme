// `self.x.call(self)` inside `Drop for Z<T>` relates `self: &mut Z<T>` to the
// method's `&U`.  Upstream tries unsizing first, and with the target `?U` still
// unknown the `Unsize` obligation is ambiguous - which it takes as "no unsizing",
// falling through to the reborrow that simply unifies `Z<T>` with `?U`.  Treating
// the unknown target as a deferred unsizing left the call unresolved for good.

trait X {
    fn call<T: std::fmt::Debug>(&self, x: &T);
    fn default_method<T: std::fmt::Debug>(&self, x: &T) {
        println!("X::default_method {:?}", x);
    }
}

#[derive(Debug)]
struct Y(#[allow(dead_code)] isize);

#[derive(Debug)]
struct Z<T: X+std::fmt::Debug> {
    x: T
}

impl X for Y {
    fn call<T: std::fmt::Debug>(&self, x: &T) {
        println!("X::call {:?} {:?}", self, x);
    }
}

impl<T: X + std::fmt::Debug> Drop for Z<T> {
    fn drop(&mut self) {
        // These statements used to cause an ICE.
        self.x.call(self);
        self.x.default_method(self);
    }
}

pub fn main() {
    let _z = Z {x: Y(42)};
}
