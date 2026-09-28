// wasmparser's `Remap::insert_if_any_changed<T>(&mut self, .., id: &mut T::Id, ty: T)`
// is called from another default method of the trait before `T` is known from
// `ty`. rustc normalizes the instantiated signature first (`confirm_method`): a
// projection on a still-unknown `T`, wherever it stands, becomes a fresh
// variable owed to it, and `&mut u32` coerces into `&mut ?U`. Only a parameter
// that was itself such a projection got that treatment, so `&mut T::Id` left
// the call ambiguous for good.
trait TypeData {
    type Id: Copy;

    fn next(&self, id: Self::Id) -> Self::Id;
}

struct FuncType;

impl TypeData for FuncType {
    type Id = u32;

    fn next(&self, id: u32) -> u32 {
        id + 1
    }
}

trait Remap {
    fn insert<T>(&mut self, id: &mut T::Id, ty: T) -> bool
    where
        T: TypeData,
    {
        *id = ty.next(*id);
        true
    }

    fn remap_func(&mut self, id: &mut u32) -> bool {
        self.insert(id, FuncType)
    }
}

fn through<X: Remap>(x: &mut X, id: &mut u32) -> bool {
    x.insert(id, FuncType)
}

struct Alloc;

impl Remap for Alloc {}

fn main() {
    let mut id = 0;
    assert!(Alloc.remap_func(&mut id));
    assert!(through(&mut Alloc, &mut id));
    assert_eq!(id, 2);
}
