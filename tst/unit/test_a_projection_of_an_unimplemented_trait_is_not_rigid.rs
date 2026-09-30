// arc-swap's `Access<T>` has a blanket `impl<T, A: Access<T> + ?Sized, P:
// Deref<Target = A>> Access<T> for P` beside `impl Access<R> for Map<A, T, F>`.
// For `Map<..>: Access<?T>` upstream drops the blanket candidate at once:
// `Map: Deref` has no impl, so `<Map as Deref>::Target` cannot be normalized
// and the projection bound fails. We read that projection as rigid, the blanket
// candidate asked `<Map as Deref>::Target: Access<?T>`, which is the blanket
// again one `Deref` deeper, and the arc-swap test crate never finished.
use std::marker::PhantomData;
use std::ops::Deref;

pub trait Access<T> {
    type Guard: Deref<Target = T>;
    fn load(&self) -> Self::Guard;
}

impl<T, A: Access<T> + ?Sized, P: Deref<Target = A>> Access<T> for P {
    type Guard = A::Guard;
    fn load(&self) -> Self::Guard {
        self.deref().load()
    }
}

pub trait DynAccess<T> {
    fn load(&self) -> Box<dyn Deref<Target = T>>;
}

impl<T, A> DynAccess<T> for A
where
    A: Access<T>,
    A::Guard: 'static,
{
    fn load(&self) -> Box<dyn Deref<Target = T>> {
        Box::new(Access::load(self))
    }
}

pub struct Constant<T>(pub T);

pub struct ConstantDeref<T>(T);

impl<T> Deref for ConstantDeref<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.0
    }
}

impl<T: Clone> Access<T> for Constant<T> {
    type Guard = ConstantDeref<T>;
    fn load(&self) -> Self::Guard {
        ConstantDeref(self.0.clone())
    }
}

pub struct Map<A, T, F> {
    access: A,
    projection: F,
    _t: PhantomData<fn() -> T>,
}

impl<A, T, F> Map<A, T, F> {
    pub fn new<R>(access: A, projection: F) -> Self
    where
        F: Fn(&T) -> &R + Clone,
    {
        Map { access, projection, _t: PhantomData }
    }
}

pub struct MapGuard<G, F, T, R> {
    guard: G,
    projection: F,
    _t: PhantomData<fn(&T) -> &R>,
}

impl<G, F, T, R> Deref for MapGuard<G, F, T, R>
where
    G: Deref<Target = T>,
    F: Fn(&T) -> &R,
{
    type Target = R;
    fn deref(&self) -> &R {
        (self.projection)(&self.guard)
    }
}

impl<A, F, T, R> Access<R> for Map<A, T, F>
where
    A: Access<T>,
    F: Fn(&T) -> &R + Clone,
{
    type Guard = MapGuard<A::Guard, F, T, R>;
    fn load(&self) -> Self::Guard {
        MapGuard { guard: self.access.load(), projection: self.projection.clone(), _t: PhantomData }
    }
}

#[derive(Clone)]
struct Inner {
    val: usize,
}

#[derive(Clone)]
struct Outer {
    inner: Inner,
}

fn load_any<T, A: Access<T>>(a: &A) -> A::Guard {
    a.load()
}

fn main() {
    let map = Map::new(Constant(Outer { inner: Inner { val: 42 } }), |outer: &Outer| &outer.inner);
    let guard = load_any(&map);
    assert_eq!(42, guard.val);
    let nested = Map::new(map, |inner: &Inner| &inner.val);
    assert_eq!(42, *load_any(&nested));
}
